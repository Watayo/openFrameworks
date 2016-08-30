#pragma once

#include <vulkan/vulkan.h>
#include "vk/Shader.h"
#include "vk/ShaderManager.h"
#include "vk/Pipeline.h"
#include "ofMatrix4x4.h"
#include "ofMesh.h"

/// Context manages all transient state
/// + transformation matrices
/// + material 
/// + geometry bindings
/// transient state is tracked and accumulated in CPU memory
/// before frame submission, state is flushed to GPU memory

/*

Context exists to provide legacy support for immediate mode 
style rendering.

You draw inside a context and can expect it to work
in a similar way to OpenGL immediate mode. But without the OpenGL 
"under the hood" driver optimisations.

It may be possible to use context to pre-record memory
and command buffers - and to use this to playback "canned" 
command buffers.

For this to work, you would use a static context - a context 
with one frame of backing memory - which is transferred from 
host memory to GPU memory before being used to draw.

*/


class ofVkRenderer;

namespace of {
namespace vk {


class Texture;  // ffdecl.
class Allocator; // ffdecl.

/// \brief  Context stores any transient data
/// \detail Context tracks state between begin() and end(), mimicking 
///         legacy "immediate mode" renderer behaviour
///
/// The context holds a number of frames, dependent on the 
/// number of virtual frames it holds in-flight at any time. 
/// For each virtual frame there is memory backing the current uniform
/// and dynamic vertex state. 
///
/// The context has one allocator, which holds one buffer which is backed
/// by one large chunk device memory. Device memory is segmented into 
/// equal sized parts, one part for each memory frame.
///
/// You tell the context which frame to operate on by passing the virtual 
/// frame index when calling Context::begin()

class Context
{

public:

	struct Settings
	{
		VkDevice                   device = nullptr;
		size_t                     numVirtualFrames = 0;
		shared_ptr<ShaderManager>  shaderManager;
		VkRenderPass               defaultRenderPass;
		// context is initialised with a vector of shaders
		// all these shaders contribute to the shared pipeline layout 
		// for this context. The shaders need to be compatible in their
		// sets/bindings so that there can be a shared pipeline layout 
		// for the whole context.
	} const mSettings;

private:
	
	// alias into mSettings
	const shared_ptr<ShaderManager>& mShaderManager = mSettings.shaderManager;

	// allocator used for dynamic data
	shared_ptr<of::vk::Allocator> mAlloc;

	// -----------	Frame state CPU memory backing

	// TODO: rename to UboBufferData
	struct UniformBufferData
	{
		int32_t  stackId = -1;
		uint32_t memoryOffset = 0;	// gpu memory offset once data has been stored
		std::vector<uint8_t> data;  // this is the data - size of this vector depends on struct_size received from spirV-cross, this is the size for the whole binding / for the whole ubo struct.
	};

	struct UboStack
	{
		uint32_t struct_size =  0;    // size in bytes of UniformBufferData.data vec
		int32_t  lastSavedStackId = -1;    // rolling count of elements saved to stack
		UniformBufferData state;
		
		std::string name; // name for this UniformBuffer Block
		std::list<UniformBufferData> stateStack;
		
		void reset(){
			stateStack.clear();
			lastSavedStackId   = -1;
			state.memoryOffset = 0;
			state.stackId      = -1;
			state.data.resize( struct_size, 0 );
		}

		void push(){
			stateStack.push_back( state );
			state.stackId = -1;
		}
		
		void pop(){
			if ( stateStack.empty() ){
				ofLog() << "cannot pop empty uniform buffer state";
				return;
			}
			state = stateStack.back();
			stateStack.pop_back();			
		}

	};

	// TODO: rename to UboMemberBacking
	struct UboBindingInfo   // sub-element of a binding witin a set
	{
		// the important thing here is that a binding can have multiple uniforms
		// (or UBO members as they are called in SPIR-V)

		// but one uniform can only belong to one binding.

		UboStack* buffer;     // this points to the buffer that will be affected by this binding - there is one buffer for each binding,
		                      // this is the index into the bufferOffsets vector for the shader layout this binding belongs to.

		uint32_t offset;
		uint32_t range;
	};


	struct Frame
	{
		// all bindings for this context indexed by uboMeta Hash
		std::map<uint64_t, UboStack> uboState;

		// dictionary of ubo names to pointer to UboStack held by uboState
		std::map<std::string, UboStack*> uboNames;

		// map from uniform name to uniformMember
		std::map<std::string, UboBindingInfo> mUboMembers;

		// current binding offset into GPU memory
		// this needs to be reset every frame
		std::vector< uint32_t> bindingOffsets;

		// --- non-dynamic state

		// map from texture name to texture
		std::map<std::string, std::shared_ptr<of::vk::Texture>> mUniformTextures;

		// whether frame state has to be rebuilt
		bool initialised = false; 
	};

	// We hold stacks of CPU memory as a backing and readsource for lazy GPU uploads on draw
	// for all currently bound descriptorsets
	Frame mCurrentFrameState;

	// -----------

	int mFrameIndex = 0;
	
	// --------- pipeline info

	// TODO: pipeline cache should be shared over all contexts.
	VkPipelineCache       mPipelineCache = nullptr;

	// object which tracks current pipeline state
	// and creates a pipeline.
	GraphicsPipelineState mCurrentGraphicsPipelineState;

	struct PipelineLayoutState
	{
		// vector of descriptorSets, each with a map of bindings
		std::vector<std::map<uint32_t, uint64_t>> bindingState;
		
		// current DescriptorSetLayout hashes forming the PipelineLayout
		std::vector<uint64_t       > setLayoutKeys;

		// derived sets (nullptr if not yet initialised)
		std::vector<VkDescriptorSet> vkDescriptorSets;
		
		// vector of sets which have been invalidated
		std::vector<size_t> dirtySetIndices;

		// map from descriptorSetLayoutHash to DescriptorSet
		std::map<uint64_t, VkDescriptorSet> descriptorSetCache;

		// map from descriptorSetLayoutHash to binding table
		std::map<uint64_t, std::map<uint32_t, uint64_t>> bindingStateCache;

	};

	// State and caches for current frame - this is reset every time 
	// the context begins recording a new frame.
	PipelineLayoutState mPipelineLayoutState;

	void updateDescriptorSetState();

	void updateDescriptorSets( const std::vector<size_t>& setIndices );

	VkPipeline mCurrentVkPipeline = nullptr;

	// One DescriptorPool per SwapChain frame.
	std::vector<VkDescriptorPool> mDescriptorPool;

	// list of overspill pools per swapchain frame for
	// when pool is too small and new pool needs to be allocated.
	std::vector<std::vector<VkDescriptorPool>> mDescriptorPoolOverspillPools;

	// bitfield indicating whether the descriptor pool for a virtual frame is dirty 
	// each bit represents a virtual frame index. 
	// we're not expecting more than 64 virtual frames (more than 3 seldom make sense)
	uint64_t mDescriptorPoolsDirty = -1; // all bits '1' == all dirty

	// Number of descriptors per type, one (or more) vector entries per descriptor type
	std::vector<VkDescriptorPoolSize> mDescriptorPoolSizes;

	// Max number of sets which can be allocated from the main per-frame descriptor pool
	uint32_t mDescriptorPoolMaxSets = 0;

	// Create one descriptor pool per virtual frame - each with enough space
	// to allocate all descriptors enumerated in mDescriptorPoolSizes
	void setupDescriptorPools();

	// Reset descriptorPool for this frame - and if descriptorSets were created 
	// for this frame using overspill descriptor pools, a new, consolidated 
	// descriptorPool (capable of holding everything) is created.
	void resetDescriptorPool( size_t frame_ );

	// Map from pipeline state hash to VkPipeline object
	std::map<uint64_t, VkPipeline> mVkPipelines;

	// all shaders attached to this context
	std::vector<std::shared_ptr<of::vk::Shader>> mShaders;

	// sets up backing memory to track state, based on shaders
	void initialiseFrameState();

public:


	// Context must be constructed using this method, default constructor,
	// copy- and move-constructor have been implicitly deleted,
	// by defining mSettings const.
	Context( const of::vk::Context::Settings& settings_ );

	~Context();

	void addShader( std::shared_ptr<of::vk::Shader> shader_ );

	// allocates memory on the GPU for each swapchain image (call rarely)
	void setup( ofVkRenderer* renderer );

	// destroys memory allocations
	void reset();

	/// map uniform buffers so that they can be written to.
	/// \return an address into gpu readable memory
	/// also resets indices into internal matrix state structures
	void begin( size_t frame_ );

	// unmap uniform buffers 
	void end();

	// write current descriptor buffer state to GPU buffer
	// updates descriptorOffsets - saves these in frameShadow
	void flushUniformBufferState();

	void bindDescriptorSets( const VkCommandBuffer & cmd );
	void bindPipeline( const VkCommandBuffer& cmd );

	Context& setShader( const std::shared_ptr<of::vk::Shader>& shader_ );
	Context& setRenderPass( const VkRenderPass& renderpass_ );

	// return the one buffer which is used for all dynamic buffer memory within this context.
	const VkBuffer& getVkBuffer() const;

	const std::shared_ptr<ShaderManager> & getShaderManager(){
		return mShaderManager;
	}

	// lazily store uniform data into local CPU memory
	template<typename UniformT>
	Context& setUniform( const std::string& name_, const UniformT & pSource );

	// fetch uniform
	template<typename UniformT>
	UniformT & getUniform( const std::string & name_ );

	// fetch const uniform
	template<typename UniformT>
	const UniformT & getUniform( const std::string & name_ ) const;

	inline const glm::mat4x4 & getViewMatrix()       const { return getUniform<glm::mat4x4>( "viewMatrix"       ); }
	inline const glm::mat4x4 & getModelMatrix()      const { return getUniform<glm::mat4x4>( "modelMatrix"      ); }
	inline const glm::mat4x4 & getProjectionMatrix() const { return getUniform<glm::mat4x4>( "projectionMatrix" ); }

	Context& setViewMatrix( const glm::mat4x4& mat_ );
	Context& setProjectionMatrix( const glm::mat4x4& mat_ );

	Context& translate(const glm::vec3& v_);
	Context& rotateRad( const float & degrees_, const glm::vec3& axis_ );

	// push local ubo uniform group state
	Context& pushBuffer( const std::string& ubo_ );
	
	// pop local ubo uniform group state
	Context& popBuffer( const std::string& ubo_ );

	// push currentMatrix state
	Context& pushMatrix(){
		pushBuffer( "DefaultMatrices" );
		return *this;
	}
	// pop current Matrix state
	Context& popMatrix(){
		popBuffer( "DefaultMatrices" );
		return *this;
	}

	// draw a mesh using current context draw state
	Context& draw(const VkCommandBuffer& cmd, const ofMesh& mesh_);

	// store vertex and index data inside the current dynamic memory frame
	// return memory mapping offets based on current memory buffer.
	bool storeMesh( const ofMesh& mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets );
	
	Context& setPolyMode( VkPolygonMode polyMode_){
		mCurrentGraphicsPipelineState.setPolyMode( polyMode_ );
		return *this;
	}

	Context& bindTexture( std::shared_ptr<of::vk::Texture> tex, const std::string & name);

};

// ----------------------------------------------------------------------

template<typename UniformT>
inline Context& Context::setUniform( const std::string & name_, const UniformT & uniform_ ){
	auto uboIt = mCurrentFrameState.mUboMembers.find( name_ );
	if ( uboIt == mCurrentFrameState.mUboMembers.end() ){
		ofLogWarning() << "Cannot set uniform: '" << name_ << "' - Not found in shader.";
		return *this;
	}
	auto & ubo = uboIt->second;
	if ( sizeof( UniformT ) != ubo.range ){
		// assignment would overshoot - possibly wrong type for assignment : refuse assignment.
		ofLogWarning() << "Cannot assign to uniform: '" << name_ << "' - data size is incorrect: " << sizeof( UniformT ) << " Byte, expected: " << ubo.range << "Byte";
		return *this;
	}
	UniformT& uniform = reinterpret_cast<UniformT&>( ( ubo.buffer->state.data[ubo.offset] ) );
	uniform = uniform_;
	ubo.buffer->state.stackId = -1; // mark dirty
	return *this;
};

// ----------------------------------------------------------------------

template<typename UniformT>
inline UniformT& Context::getUniform( const std::string & name_ ){
	static UniformT errUniform;
	auto uboIt = mCurrentFrameState.mUboMembers.find( name_ );
	if ( uboIt == mCurrentFrameState.mUboMembers.end() ){
		ofLogWarning() << "Cannot get uniform: '" << name_ << "' - Not found in shader.";
		return errUniform;
	}
	auto & ubo = uboIt->second;
	if ( sizeof( UniformT ) != ubo.range ){
		// assignment would overshoot - return a default uniform.
		ofLogWarning() << "Cannot get uniform: '" << name_ << "' - data size is incorrect: " << sizeof( UniformT ) << " Byte, expected: " << ubo.range << "Byte";
		return errUniform;
	}
	UniformT& uniform = reinterpret_cast<UniformT&>( ( ubo.buffer->state.data[ubo.offset] ) );
	ubo.buffer->state.stackId = -1; // mark dirty
	return uniform;
};

// ----------------------------------------------------------------------

template<typename UniformT>
inline const UniformT& Context::getUniform( const std::string & name_ ) const {
	static const UniformT errUniform;
	auto uboIt = mCurrentFrameState.mUboMembers.find( name_ );
	if ( uboIt == mCurrentFrameState.mUboMembers.end() ){
		ofLogWarning() << "Cannot get uniform: '" << name_ << "' - Not found in shader.";
		return errUniform;
	}
	auto & ubo = uboIt->second;
	if ( sizeof( UniformT ) != ubo.range ){
		// assignment would overshoot - return a default uniform.
		ofLogWarning() << "Cannot get uniform: '" << name_ << "' - data size is incorrect: " << sizeof( UniformT ) << " Byte, expected: " << ubo.range << "Byte";
		return errUniform;
	}
	const UniformT& uniform = reinterpret_cast<UniformT&>( ( ubo.buffer->state.data[ubo.offset] ) );
	return uniform;
};

} // namespace vk
} // namespace of
