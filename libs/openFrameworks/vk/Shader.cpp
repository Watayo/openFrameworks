#include "vk/Shader.h"
#include "ofLog.h"
#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "spooky/SpookyV2.h"
#include "shaderc/shaderc.hpp"
#include <algorithm>

// ----------------------------------------------------------------------

namespace of{ 
namespace utils{

// static utility method : no-op on non-WIN32 system. 
void setConsoleColor( uint32_t colour = 12 ){
#ifdef WIN32
	static HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
	SetConsoleTextAttribute( hConsole, colour + 0 * 16 );
#endif 
}

// reset console color
// static utility method : no-op on non-WIN32 system. 
void resetConsoleColor(){
#ifdef WIN32
	static HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
	SetConsoleTextAttribute( hConsole, 7 + 0 * 16 );
#endif 
}

} /*namespace vk*/ 
} /*namespace of*/


// ----------------------------------------------------------------------

of::vk::Shader::Shader( const ::vk::Device& device_, const std::map<::vk::ShaderStageFlagBits, std::string>& sources )
	: mSettings( { device_, sources } )
{
	compile();
}

// ----------------------------------------------------------------------

const uint64_t of::vk::Shader::getShaderCodeHash(){
	if ( mShaderHashDirty ){
		std::vector<uint64_t> spirvHashes;
		spirvHashes.reserve( mSpvHash.size() );
		for ( const auto&k : mSpvHash ){
			spirvHashes.push_back( k.second );
		}
		mShaderHash = SpookyHash::Hash64( spirvHashes.data(), spirvHashes.size() * sizeof( uint64_t ), 0 );
		mShaderHashDirty = false;
	}
	return mShaderHash;
}

// ----------------------------------------------------------------------

void of::vk::Shader::compile(){
	bool shaderDirty = false;
	
	for ( auto & source : mSettings.sources ){

		const auto & shaderType = source.first;
		const auto & filename = source.second;

		if ( !ofFile( filename ).exists() ){
			ofLogFatalError() << "Shader file not found: " << source.second;
			ofExit(1);
			return;
		}

		std::vector<uint32_t> spirCode;
		bool success = getSpirV( shaderType, filename, spirCode );	/* load or compiles code into spirCode */

		if ( !success){
			if (!mShaderStages.empty()){
				ofLogError() << "Aborting shader compile. Using previous version of shader instead";
				return;
			} else{
				// !TODO: should we use a default shader, then?
				ofLogFatalError() << "Shader did not compile: " << filename;
				ofExit( 1 );
			}
		} 

		uint64_t spirvHash = SpookyHash::Hash64( reinterpret_cast<char*>( spirCode.data() ), spirCode.size() * sizeof( uint32_t ), 0 );

		bool spirCodeDirty = isSpirCodeDirty( shaderType, spirvHash );

		if ( spirCodeDirty ){
			ofLog() << "Building shader module: " << filename;
			createVkShaderModule( shaderType, spirCode );
			// store hash in map so it does not appear dirty
			mSpvHash[shaderType] = spirvHash;
			// move the ir code buffer into the shader compiler
			mSpvCrossCompilers[shaderType] = make_shared<spirv_cross::Compiler>( std::move( spirCode ) );
		}

		shaderDirty |= spirCodeDirty;
		mShaderHashDirty |= spirCodeDirty;
	}

	if ( shaderDirty ){
		reflect( mSpvCrossCompilers, mVertexInfo );
		createSetLayouts();
		createVkPipelineLayout();
		shaderDirty = false;
	}
	
}

// return shader stage information for pipeline creation



// ----------------------------------------------------------------------

bool of::vk::Shader::isSpirCodeDirty( const ::vk::ShaderStageFlagBits shaderStage, uint64_t spirvHash ){

	if ( mSpvHash.find( shaderStage ) == mSpvHash.end() ){
		// hash not found so must be dirty
		return true;
	} else{
		return ( mSpvHash[shaderStage] != spirvHash );
	}
	
	return false;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::getSpirV( const ::vk::ShaderStageFlagBits shaderStage, const std::string & fileName, std::vector<uint32_t> &spirCode ){
	
	auto f = ofFile( fileName );
	auto fExt = f.getExtension();

	if ( fExt == "spv" ){
		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		ofLogNotice() << "Loading SPIR-V shader module: " << fileName;
		auto a = fileBuf.getData();
		spirCode.assign(
			reinterpret_cast<uint32_t*>( fileBuf.getData() ),
			reinterpret_cast<uint32_t*>( fileBuf.getData() ) + fileBuf.size() / sizeof( uint32_t )
		);
		return true;
	} else {
		shaderc_shader_kind shaderType = shaderc_shader_kind::shaderc_glsl_infer_from_source;

		switch ( shaderStage ){
		case ::vk::ShaderStageFlagBits::eVertex :
			shaderType = shaderc_shader_kind::shaderc_glsl_default_vertex_shader;
			break;
		case ::vk::ShaderStageFlagBits::eFragment : 
			shaderType = shaderc_shader_kind::shaderc_glsl_default_fragment_shader;
			break;
		default:
			break;
		}

		bool success = true;

		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;

		// Like -DMY_DEFINE=1
		// options.AddMacroDefinition( "MY_DEFINE", "1" );

		shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
			fileBuf.getData(), fileBuf.size(), shaderType, fileName.c_str(), options );

		if ( module.GetCompilationStatus() != shaderc_compilation_status_success ){
			std::string errorMessage = module.GetErrorMessage();
			ofLogError() << "Shader compile failed for: " << fileName;

			of::utils::setConsoleColor( 12 /* red */ );
			ofLogError() << std::endl << errorMessage;
			of::utils::resetConsoleColor();
			 
			// Error string will have the form:  "triangle.frag:28: error: '' :  syntax error"

			ostringstream scanString; /* create a scan string with length of first element known: "%*23s : %d :" */
			scanString << "%*" << fileName.size() << "s : %d :"; 
			
			uint32_t lineNumber = 0; /* <- Will contain error line number after successful parse */
			auto scanResult = sscanf( errorMessage.c_str(), scanString.str().c_str(), &lineNumber );

			if ( scanResult != std::char_traits<wchar_t>::eof() ){
				auto lineIt = fileBuf.getLines().begin();
				size_t currentLine = 1; /* Line numbers start counting at 1 */

				while (lineIt != fileBuf.getLines().end()){

					if ( currentLine >= lineNumber - 3 ){
						ostringstream sourceContext;
						const auto shaderSourceCodeLine = lineIt.asString();
						sourceContext << std::right << std::setw(4) << currentLine << " | " << shaderSourceCodeLine;
						
						if ( currentLine == lineNumber ) of::utils::setConsoleColor( 11 );
						ofLogError() << sourceContext.str();
						if ( currentLine == lineNumber ) of::utils::resetConsoleColor();
					}

					if ( currentLine >= lineNumber + 2 ){
						ofLogError(); // add empty for better readability
						break;
					}

					++lineIt;
					++currentLine;
				}
			}

			return false;
		} else{
			spirCode.clear();
			spirCode.assign( module.cbegin(), module.cend() );
			// ofLogNotice() << "OK \tShader compile: " << fileName;
			return true;
		}
		
		assert( success );
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkShaderModule( const ::vk::ShaderStageFlagBits shaderType, const std::vector<uint32_t> &spirCode ){

	::vk::ShaderModuleCreateInfo shaderModuleCreateInfo; 
	shaderModuleCreateInfo
		.setFlags( ::vk::ShaderModuleCreateFlagBits() )
		.setCodeSize( spirCode.size() * sizeof(uint32_t))
		.setPCode( spirCode.data() )
		;

	::vk::ShaderModule module = mSettings.device.createShaderModule( shaderModuleCreateInfo );

	auto tmpShaderStage = std::shared_ptr<ShaderStage>( new ShaderStage, [d = mSettings.device](ShaderStage* lhs){
		d.destroyShaderModule( lhs->module );
		delete lhs;
	} );

	tmpShaderStage->module = module;

	tmpShaderStage->createInfo = ::vk::PipelineShaderStageCreateInfo();
	tmpShaderStage->createInfo
		.setStage( shaderType )
		.setModule( tmpShaderStage->module )
		.setPName( "main" )
		.setPSpecializationInfo( nullptr )
		;

	mShaderStages[shaderType] = std::move( tmpShaderStage );
}

// ----------------------------------------------------------------------

void of::vk::Shader::reflect(
	const std::map<::vk::ShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>>& compilers, 
	VertexInfo& vertexInfo
){
	// storage for reflected information about UBOs

	// for all shader stages
	for ( auto &c : compilers ){

		auto & compiler    = *c.second;
		auto & shaderStage = c.first;

		// ! TODO: process texture samplers
		// This: http://gpuopen.com/wp-content/uploads/2016/03/VulkanFastPaths.pdf
		// suggests one fast path is to bind all (!) textures into ONE DescriptorSet / binding 
		// as an array of textures, and then use pushConstants to fetch the index 
		// into the array for the texture we want for this particular draw. 
		// This would mean to create one descriptor per texture and to bind all these 
		// texture descriptors to one binding - and to one descriptorset.

		// --- uniform buffers ---
		reflectUBOs( compiler, shaderStage );
		
		// --- samplers
		reflectSamplers( compiler, shaderStage );

		// --- vertex inputs ---
		if ( shaderStage == ::vk::ShaderStageFlagBits::eVertex ){
			reflectVertexInputs(compiler, vertexInfo );
		} 
		
	}  

}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectUBOs( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage ){

	auto uniformBuffers = compiler.get_shader_resources().uniform_buffers;

	for ( const auto & ubo : uniformBuffers ){

		Uniform_t tmpUniform;
		tmpUniform.uboRange.storageSize = compiler.get_declared_struct_size( compiler.get_type( ubo.type_id ) );
				
		tmpUniform.setLayoutBinding
			.setDescriptorCount( 1 )                                            /* Must be 1 for ubo bindings, as arrays of ubos are not allowed */
			.setDescriptorType( ::vk::DescriptorType::eUniformBufferDynamic )   /* All our uniform buffer are dynamic */
			.setStageFlags( shaderStage )
			;

		getSetAndBindingNumber( compiler, ubo, tmpUniform.setNumber, tmpUniform.setLayoutBinding.binding);

		auto bufferRanges = compiler.get_active_buffer_ranges( ubo.id );

		for ( const auto &r : bufferRanges ){
			// Note that SpirV-Cross will only tell us the ranges of *actually used* members within an UBO. 
			// By merging the ranges later, we effectively also create aliases for member names which are 
			// not consistently named the same.
			auto memberName = compiler.get_member_name( ubo.type_id, r.index );
			tmpUniform.uboRange.subranges[memberName] = { (uint32_t)r.offset, (uint32_t)r.range };
		}

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto insertion = mUniforms.insert( { ubo.name, std::move( tmpUniform )  } );

		if (insertion.second == false ){
			// Uniform with this key already existed, nothing was inserted.

			auto & storedUniform = insertion.first->second;

			if ( storedUniform.uboRange.storageSize != tmpUniform.uboRange.storageSize ){
				ofLogWarning() << "Ubo: '" << ubo.name << "' re-defined with incompatible storage size.";
				// !TODO: try to recover.
				return false;
			} else if ( storedUniform.setNumber != tmpUniform.setNumber
				|| storedUniform.setLayoutBinding.binding != tmpUniform.setLayoutBinding.binding ){
				ofLogWarning() << "Ubo: '" << ubo.name << "' re-defined with inconsistent set/binding numbers.";
			} else {
				// Merge stage flags
				storedUniform.setLayoutBinding.stageFlags |= tmpUniform.setLayoutBinding.stageFlags;
				// Merge memberRanges
				ostringstream overlapMsg;
				if ( checkMemberRangesOverlap( storedUniform.uboRange.subranges, tmpUniform.uboRange.subranges, overlapMsg ) ){
					// member ranges overlap: print diagnostic message
					ofLogWarning() << "Inconsistency found parsing UBO: '" << ubo.name << "': " << std::endl << overlapMsg.str();
				}
				// insert any new subranges if necesary.
				storedUniform.uboRange.subranges.insert( tmpUniform.uboRange.subranges.begin(), tmpUniform.uboRange.subranges.end() );
			}
		}

	} // end: for all uniform buffers

	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectSamplers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage ){

	auto sampledImages = compiler.get_shader_resources().sampled_images;

	for ( const auto & sampledImage : sampledImages){

		Uniform_t tmpUniform;
		tmpUniform.setLayoutBinding
			.setDescriptorCount( 1 ) //!TODO: find out how to query array size
			.setDescriptorType( ::vk::DescriptorType::eCombinedImageSampler )
			.setStageFlags( shaderStage )
			;

		getSetAndBindingNumber( compiler, sampledImage, tmpUniform.setNumber, tmpUniform.setLayoutBinding.binding );

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto result = mUniforms.insert( { sampledImage.name, tmpUniform } );

		if ( result.second == false ){
			// uniform with this key already exists: check set and binding numbers are identical
			// otherwise print a warning and return false.
			if ( result.first->second.setLayoutBinding.binding != tmpUniform.setLayoutBinding.binding
				|| result.first->second.setNumber != tmpUniform.setNumber ){
				ofLogWarning() << "Uniform: '" << sampledImage.name << "' is declared multiple times, but with inconsistent binding/set number.";
				return false;
			}
		}

	} // end: for all uniform buffers

	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::createSetLayouts(){
	
	// Consolidate uniforms into descriptor sets

	// map from descriptorSet to map of bindings
	map<uint32_t, map<uint32_t, ::vk::DescriptorSetLayoutBinding>> descriptorSetLayouts;

	if ( mUniforms.empty() ){
		// nothing to do.
		return true;
	}

	// --------| invariant: there are uniforms to assign to descriptorsets.

	for ( const auto & uniform : mUniforms ){
		
		const std::pair<uint32_t, ::vk::DescriptorSetLayoutBinding> binding = {uniform.second.setLayoutBinding.binding, uniform.second.setLayoutBinding};

		// attempt to insert a fresh set
		auto setInsertion = descriptorSetLayouts.insert( { uniform.second.setNumber,{ binding } } );

		if ( setInsertion.second == false ){
		// if there was already a set at this position, append to this set
			auto bindingInsertion = setInsertion.first->second.insert( binding );
			if ( bindingInsertion.second == false ){
				ofLogError() << "Could not insert binding - it appears that there is already a binding a this position, set: " << uniform.second.setNumber 
					<< ", binding number: " << uniform.second.setLayoutBinding.binding;
				return false;
			}
		}
	}

	// ---------| invariant: we should have a map of sets and each set should have bindings
	//            and both in ascending order.

	// we need to make sure set numbers are not sparse.
	if ( descriptorSetLayouts.size() != (descriptorSetLayouts.rbegin()->first + 1)){
		ofLogError() << "Descriptor sets may not be sparse";
		return  false;
	}

	mDescriptorSetsInfo.clear();
	mDescriptorSetsInfo.reserve( descriptorSetLayouts.size() );
	mDescriptorSetLayoutKeys.clear();
	mDescriptorSetLayoutKeys.reserve( descriptorSetLayouts.size() );

	for (const auto & descriptorSet : descriptorSetLayouts ){
		DesciptorSetLayoutInfo layoutInfo;
		layoutInfo.bindings.reserve( descriptorSet.second.size() );
		for ( const auto & binding : descriptorSet.second ){
			layoutInfo.bindings.push_back( binding.second );
		}

		static_assert(
			+sizeof( ::vk::DescriptorSetLayoutBinding::binding )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::descriptorType )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::descriptorCount )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::stageFlags )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::pImmutableSamplers )
			== sizeof( ::vk::DescriptorSetLayoutBinding )
			, "DescriptorSetLayoutBindings is not tightly packed." );

		layoutInfo.hash = SpookyHash::Hash64(layoutInfo.bindings.data(),layoutInfo.bindings.size() * sizeof( ::vk::DescriptorSetLayoutBinding ), 0);
		
		mDescriptorSetLayoutKeys.push_back( layoutInfo.hash );
		mDescriptorSetsInfo.emplace_back( std::move( layoutInfo ) );
	}

	// -------| invariant: mDescriptorSetInfo contains information for each descriptorset.

	mDescriptorSetLayouts.clear();
	mDescriptorSetLayouts.reserve( mDescriptorSetsInfo.size() );

	for ( auto & descriptorSetInfo : mDescriptorSetsInfo ){
		::vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;
		descriptorSetLayoutCreateInfo
			.setBindingCount( descriptorSetInfo.bindings.size() )
			.setPBindings( descriptorSetInfo.bindings.data() )
			;

		// Create the auto-deleter
		std::shared_ptr<::vk::DescriptorSetLayout> vkDescriptorSetLayout =
			std::shared_ptr<::vk::DescriptorSetLayout>( new ::vk::DescriptorSetLayout, [d = mSettings.device]( ::vk::DescriptorSetLayout* lhs ){
			if ( lhs ){
				d.destroyDescriptorSetLayout( *lhs );
			}
			delete lhs;
		} );
		
		// create new descriptorSetLayout
		*vkDescriptorSetLayout = mSettings.device.createDescriptorSetLayout( descriptorSetLayoutCreateInfo );
		
		// store new descriptorSetLayout
		mDescriptorSetLayouts.emplace_back( std::move( vkDescriptorSetLayout ) );
	}

	return true;
}
// ----------------------------------------------------------------------

void of::vk::Shader::getSetAndBindingNumber( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & resource, uint32_t &descriptor_set, uint32_t &bindingNumber ){
	// see what kind of decorations this resource has
	uint64_t decorationMask = compiler.get_decoration_mask( resource.id );
	
	if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
		descriptor_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
	} else{
		// If undefined, set descriptor set id to 0. This is conformant with:
		// https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
		descriptor_set = 0;
		ofLogWarning() 
			<< "Warning: Shader uniform " << resource.name << "does not specify set id, and will " << endl
			<< "therefore be mapped to set 0 - this could have unintended consequences.";
	}

	if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
		bindingNumber = compiler.get_decoration( resource.id, spv::DecorationBinding );
	} else{
		ofLogWarning() << "Shader uniform" << resource.name << "does not specify binding number.";
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::reflectVertexInputs(const spirv_cross::Compiler & compiler, of::vk::Shader::VertexInfo& vertexInfo ){
	ofLog() << "Vertex Attribute locations";
	const auto shaderResources = compiler.get_shader_resources();

	vertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
	vertexInfo.bindingDescription.resize( shaderResources.stage_inputs.size() );

	for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){

		auto & attributeInput = shaderResources.stage_inputs[i];
		auto attributeType = compiler.get_type( attributeInput.type_id );

		uint32_t location = i; // shader location qualifier mapped to binding number

		if ( ( 1ull << spv::DecorationLocation ) & compiler.get_decoration_mask( attributeInput.id ) ){
			location = compiler.get_decoration( attributeInput.id, spv::DecorationLocation );
		}

		ofLog() << " " << ( i + 1 == shaderResources.stage_inputs.size() ? char( 192 ) : char( 195 ) ) << std::setw( 2 ) << location << " : " << attributeInput.name;


		// Binding Description: Describe how to read data from buffer based on binding number
		vertexInfo.bindingDescription[i].binding = location;  // which binding number we are describing
		vertexInfo.bindingDescription[i].stride = ( attributeType.width / 8 ) * attributeType.vecsize * attributeType.columns;
		vertexInfo.bindingDescription[i].inputRate = ::vk::VertexInputRate::eVertex;

		// Attribute description: Map shader location to pipeline binding number
		vertexInfo.attribute[i].location = location;   // .location == which shader attribute location
		vertexInfo.attribute[i].binding = location;    // .binding  == pipeline binding number == where attribute takes data from

		switch ( attributeType.vecsize ){
		case 2:
			vertexInfo.attribute[i].format = ::vk::Format::eR32G32Sfloat;        // 2-part float
			break;
		case 3:
			vertexInfo.attribute[i].format = ::vk::Format::eR32G32B32Sfloat;     // 3-part float
			break;
		case 4:
			vertexInfo.attribute[i].format = ::vk::Format::eR32G32B32A32Sfloat;	 // 4-part float
			break;
		default:
			ofLogWarning() << "Could not determine vertex attribute type for: " << attributeInput.name;
			break;
		}
	}

	::vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = ::vk::PipelineVertexInputStateCreateInfo(); 
	vertexInputStateCreateInfo
		.setVertexBindingDescriptionCount( vertexInfo.bindingDescription.size() )
		.setPVertexBindingDescriptions( vertexInfo.bindingDescription.data() )
		.setVertexAttributeDescriptionCount( vertexInfo.attribute.size() )
		.setPVertexAttributeDescriptions( vertexInfo.attribute.data() )
		;

	vertexInfo.vi = std::move( vertexInputStateCreateInfo );
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkPipelineLayout() {
	
	std::vector<::vk::DescriptorSetLayout> vkLayouts;
	vkLayouts.reserve( mDescriptorSetLayouts.size() );

	for ( const auto &layout : mDescriptorSetLayouts ){
		vkLayouts.push_back(*layout);
	}
	
	auto pipelineInfo = ::vk::PipelineLayoutCreateInfo();
	pipelineInfo
		.setSetLayoutCount( vkLayouts.size())
		.setPSetLayouts( vkLayouts.data() )
		.setPushConstantRangeCount( 0 )
		.setPPushConstantRanges( nullptr )
		;


	mPipelineLayout = std::shared_ptr<::vk::PipelineLayout>( new ::vk::PipelineLayout,
		[device = mSettings.device]( ::vk::PipelineLayout* lhs ){
		if ( lhs ){
			device.destroyPipelineLayout( *lhs );
		}
		delete lhs;
	} );

	*mPipelineLayout = mSettings.device.createPipelineLayout( pipelineInfo );

}


// ----------------------------------------------------------------------
// Check whether member ranges within an UBO overlap
// Should this be the case, there is a good chance that the 
// Ubo layout was inconsistently defined across shaders or 
// shader stages, or that there was a typo in an UBO declaration.
bool of::vk::Shader::checkMemberRangesOverlap(
	const std::map<std::string, of::vk::Shader::UboMemberSubrange>& lhs,
	const std::map<std::string, of::vk::Shader::UboMemberSubrange>& rhs,
	std::ostringstream & errorMsg ) {

	// Check whether member ranges overlap.
	// 
	// 0. combine member ranges eagerly
	// 1. sort member ranges by start
	// 2. for each sorted member range
	//    2.0 move to next if current member is exact duplicate of last member [perfect match, that's what we want.]
	//    2.1 check if current member offset ==  last member offset [overlap because start at same place]
	//    2.1 check if (last member offset + last member range) > current member offset [overlap because current starts inside last]

	bool overlap = false;

	if ( rhs.empty() ){
		// impossible that there might be a conflict if there is no second set to compare with. 
		return false;
	}

	std::vector<std::pair<std::string, of::vk::Shader::UboMemberSubrange>> ranges;
	ranges.insert( ranges.begin(), lhs.begin(), lhs.end() );
	ranges.insert( ranges.begin(), rhs.begin(), rhs.end() );

	std::sort( ranges.begin(), ranges.end(), []( const std::pair<std::string, of::vk::Shader::UboMemberSubrange> & lhs,
		std::pair<std::string, of::vk::Shader::UboMemberSubrange>&rhs )->bool{
		return lhs.second.offset < rhs.second.offset;
	} );

	auto lastRangeIt = ranges.begin();
	for ( auto rangeIt = ++ranges.begin(); rangeIt != ranges.end(); lastRangeIt = rangeIt++ ){

		if ( rangeIt->first == lastRangeIt->first
			&& rangeIt->second.offset == lastRangeIt->second.offset
			&& rangeIt->second.range == lastRangeIt->second.range
			){
			continue;
		}

		bool overlapStart = false;
		bool overlapRange = false;

		if ( rangeIt->second.offset == lastRangeIt->second.offset ){
			overlap = overlapStart = true;
		}

		if ( ( lastRangeIt->second.offset + lastRangeIt->second.range ) > rangeIt->second.offset ){
			overlap = overlapRange = true;
		}

		if ( overlapStart || overlapRange ){
			errorMsg << "Range for UBO Member Names: '" << rangeIt->first << "' and '" << lastRangeIt->first << "' overlap.";
			if ( rangeIt->second.range == lastRangeIt->second.range ){
				errorMsg << "\nCheck for a possible typo in this UBO member name.";
			} else{
				errorMsg << "\nCheck whether the elements within this UBO are laid out consistently over all shaders that use it within this Context.";
			}
		}

	}

	return overlap;
}