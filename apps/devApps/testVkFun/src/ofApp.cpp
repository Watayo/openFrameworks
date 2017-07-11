#include "ofApp.h"

#define EXAMPLE_TARGET_FRAME_RATE 60
bool isFrameLocked = false;

std::shared_ptr<ofVkRenderer> renderer = nullptr;

//--------------------------------------------------------------

void ofApp::setup(){

	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	ofDisableSetupScreen();
	ofSetFrameRate( isFrameLocked ? EXAMPLE_TARGET_FRAME_RATE : 0 );

	setupDrawCommands();

	setupMeshL();

	mMeshPly = std::make_shared<ofMesh>();
	mMeshPly->load( "ico-m.ply" );
	
	setupStaticAllocators();
	uploadStaticData( *renderer->getStagingContext() );

	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );
	mCam.setEvents( ofEvents() );
}

//--------------------------------------------------------------

void ofApp::setupStaticAllocators(){

	{

		of::vk::BufferAllocator::Settings allocatorSettings;
		allocatorSettings
			.setRendererProperties( renderer->getVkRendererProperties() )
			.setSize( 1 << 24UL ) // 16 MB
			.setFrameCount( 1 )
			.setMemFlags( ::vk::MemoryPropertyFlagBits::eDeviceLocal )
			.setBufferUsageFlags( allocatorSettings.bufferUsageFlags | ::vk::BufferUsageFlagBits::eStorageBuffer )
			;
		mStaticAllocator = std::make_unique<of::vk::BufferAllocator>( allocatorSettings );
		mStaticAllocator->setup();
	}
	{
		of::vk::ImageAllocator::Settings allocatorSettings;
		allocatorSettings
			.setRendererProperties( renderer->getVkRendererProperties() )
			.setSize( 1 << 24UL ) // 16 MB
			.setMemFlags( ::vk::MemoryPropertyFlagBits::eDeviceLocal )
			;
		mImageAllocator = std::make_unique<of::vk::ImageAllocator>( allocatorSettings );
		mImageAllocator->setup();
	}
}

//--------------------------------------------------------------

void ofApp::setupDrawCommands(){
	
	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings
			.setDevice(renderer->getVkDevice())
			.setSource( ::vk::ShaderStageFlagBits::eCompute, "compute.glsl" )
			.setPrintDebugInfo(true)
		;

		auto shaderCompute = std::make_shared<of::vk::Shader>( shaderSettings );

		of::vk::ComputePipelineState computePipeline;
		computePipeline.setShader( shaderCompute );
		const_cast<of::vk::ComputeCommand&>(computeCmd).setup( computePipeline );
	}

	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings
			.setDevice(renderer->getVkDevice())
			.setSource(::vk::ShaderStageFlagBits::eVertex  , "default.vert")
			.setSource(::vk::ShaderStageFlagBits::eFragment, "default.frag")
			.setPrintDebugInfo( true )
			;

		auto mShaderDefault = std::make_shared<of::vk::Shader>( shaderSettings );

		of::vk::GraphicsPipelineState pipeline;

		pipeline.depthStencilState
			.setDepthTestEnable( VK_TRUE )
			.setDepthWriteEnable( VK_TRUE )
			;

		pipeline.inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
		//pipeline.setPolyMode( ::vk::PolygonMode::eLine );
		pipeline.setShader( mShaderDefault );
		pipeline.blendAttachmentStates[0]
			.setBlendEnable( VK_TRUE )
			;

		const_cast<of::vk::DrawCommand&>( drawPhong ).setup( pipeline );
	}

	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings
			.setDevice( renderer->getVkDevice() )
			.setSource( ::vk::ShaderStageFlagBits::eVertex  , "fullScreenQuad.vert" )
			.setSource( ::vk::ShaderStageFlagBits::eFragment, "fullScreenQuad.frag" )
			.setPrintDebugInfo( true )
			;

		auto mShaderFullScreenQuad = std::make_shared<of::vk::Shader>( shaderSettings );
		
		of::vk::GraphicsPipelineState pipeline;

		pipeline.setShader( mShaderFullScreenQuad );
		pipeline.rasterizationState.setCullMode( ::vk::CullModeFlagBits::eFront );
		pipeline.rasterizationState.setFrontFace( ::vk::FrontFace::eCounterClockwise );
		pipeline.depthStencilState
			.setDepthTestEnable( VK_FALSE )
			.setDepthWriteEnable( VK_FALSE )
			;
		pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;
		
		const_cast<of::vk::DrawCommand&>( drawFullScreenQuad ).setup( pipeline );
		const_cast<of::vk::DrawCommand&>( drawFullScreenQuad ).setNumVertices( 3 );
	}

	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings
			.setDevice(renderer->getVkDevice())
			.setSource( ::vk::ShaderStageFlagBits::eVertex  , "textured.vert" )
			.setSource( ::vk::ShaderStageFlagBits::eFragment, "textured.frag" )
			.setPrintDebugInfo( true )
			;

		auto mShaderTextured = std::make_shared<of::vk::Shader>( shaderSettings );

		of::vk::GraphicsPipelineState pipeline;

		pipeline.setShader( mShaderTextured );
		pipeline.rasterizationState.setCullMode( ::vk::CullModeFlagBits::eBack );
		pipeline.rasterizationState.setFrontFace( ::vk::FrontFace::eCounterClockwise );
		pipeline.depthStencilState
			.setDepthTestEnable( VK_TRUE )
			.setDepthWriteEnable( VK_TRUE )
			;
		pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;

		const_cast<of::vk::DrawCommand&>( drawTextured ).setup( pipeline );
	}
}

//--------------------------------------------------------------

void ofApp::update(){

	ofSetWindowTitle( ofToString( ofGetFrameRate(), 2, ' ' ) );
	
	//if ( ofGetFrameNum() % 60 == 0){
	//	ofLog() << "Current fps: " << ofGetFrameRate();
	//}

}

//--------------------------------------------------------------

void ofApp::draw(){

	auto & currentContext = *renderer->getDefaultContext();

	

	// In Vulkan, clip space has y flipped, 
	// and z is mapped from -1..1 to 0..1 (scale 0.5, translate 0.5), 

	static const glm::mat4x4 clip ( 
		1.0f,  0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f,  0.0f, 0.5f, 0.0f,
		0.0f,  0.0f, 0.5f, 1.0f 
	);

	auto viewMatrix       = mCam.getModelViewMatrix();
	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );


	auto modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 1.f, 0.f } ) );


	// Create a fresh copy of our prototype phong draw command
	auto hero = drawPhong;
	hero
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", modelMatrix )
		.setStorageBuffer( "colorLayout", mStaticColourBuffer )
		.setIndices( mStaticMesh.indexBuffer )
		.setNumIndices( mStaticMesh.indexBuffer.numElements )
		.setDrawMethod(of::vk::DrawCommand::DrawMethod::eIndexed)
		.setAttribute( 0, mStaticMesh.posBuffer )
		.setAttribute( 1, mStaticMesh.normalBuffer )
		;

	auto texturedRect = drawTextured;
	texturedRect
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", glm::mat4() )
		.setTexture( "tex_0", *mTexture )
		.setIndices( mRectangleData.indexBuffer )
		.setNumIndices(mRectangleData.indexBuffer.numElements)
		.setDrawMethod(of::vk::DrawCommand::DrawMethod::eIndexed)
		.setAttribute( 0, mRectangleData.posBuffer )
		.setAttribute( 1, mRectangleData.texCoordBuffer )
		;

	std::vector<::vk::ClearValue> clearValues( 2 );
	clearValues[0].setColor( reinterpret_cast<const ::vk::ClearColorValue&>( ofFloatColor::black ) );
	clearValues[1].setDepthStencil( { 1.f, 0 } );

	of::vk::RenderBatch::Settings settings;
	settings.setClearValues(clearValues)
		.setContext( renderer->getDefaultContext().get() )
		.setRenderArea( ::vk::Rect2D( {}, { uint32_t( renderer->getViewportWidth() ), uint32_t( renderer->getViewportHeight() ) } ) )
		.setRenderPass( *renderer->getDefaultRenderpass() )
		.setFramebufferAttachmentsExtent( renderer->getSwapchain()->getWidth(), renderer->getSwapchain()->getHeight() )
		.addFramebufferAttachment( renderer->getDefaultContext()->getSwapchainImageView() )
		.addFramebufferAttachment( renderer->getDepthStencilImageView() )
		;

	of::vk::RenderBatch batch{ settings };
	
	batch.begin();
	batch
		.draw( drawFullScreenQuad )
		.draw( hero )
		.draw( texturedRect);
		;
	batch.end();

	// submitting the compute command after the batch has been submitted
	// means it will end up on the queue *after* the draw instructions.

	/*auto comp = computeCmd;
	comp.setStorageBuffer( "ParticleBuf", mParticlesRegion );
	uint32_t flipFlop = ofGetFrameNum() % 2;
	comp.setUniform( "flipFlop", flipFlop );
	comp.submit( currentContext, {1,1,1} );*/

}

//--------------------------------------------------------------

void ofApp::uploadStaticData( of::vk::Context & stagingContext ){

	ofMesh meshPlane = ofMesh::plane( 1024 / 2, 768 / 2, 2, 2, OF_PRIMITIVE_TRIANGLES );

	std::array<glm::vec4, 3> colourVec{{
			{1,0,0,1},
			{1,1,0,1},
			{0,0,1,1},
		}};

	struct Particle
	{
		glm::vec2 pos;
		glm::vec2 vel;
		glm::vec4 result;
	};

	std::array<Particle, 2> particleVec{
		{
			{
				{  1.f, 1.f },
				{ 0.5f, 0.5f},
				{  0.f, 0.f, 0.f, 0.f}
			},
			{
				{ 0.f, 0.f},
				{ 0.0f, 0.0f },
				{ 1.f, 1.f, 1.f, 1.f }
			}
		}
	};

	std::vector<of::vk::TransferSrcData> srcDataVec = {
		// data for our strange hero object
		{
			mMeshPly->getIndexPointer(),
			mMeshPly->getNumIndices(),
			sizeof( ofIndexType ),
		},
		{
			mMeshPly->getVerticesPointer(),
			mMeshPly->getNumVertices(),
			sizeof( ofDefaultVertexType ),
		},
		{
			mMeshPly->getNormalsPointer(),
			mMeshPly->getNumNormals(),
			sizeof( ofDefaultNormalType ),
		},
		// ---- data for textured plane
		{
			meshPlane.getIndexPointer(),
			meshPlane.getNumIndices(),
			sizeof( ofIndexType ),
		},
		{
			meshPlane.getVerticesPointer(),
			meshPlane.getNumVertices(),
			sizeof(ofDefaultVertexType),
		},
		{
			meshPlane.getTexCoordsPointer(),
			meshPlane.getNumTexCoords(),
			sizeof(ofDefaultTexCoordType)
		},
		// data for the storage buffer
		{
			colourVec.data(),
			colourVec.size(),
			sizeof(glm::vec4)
		},
		// data for the particle storage buffer
		{
			particleVec.data(),
			particleVec.size(),
			sizeof(Particle),
		}

	};

	const auto & staticBuffer = mStaticAllocator->getBuffer();

	std::vector<of::vk::BufferRegion> bufferRegions = stagingContext.storeBufferDataCmd( srcDataVec, mStaticAllocator );

	if ( bufferRegions.size() == 8 ) {
		mStaticMesh.indexBuffer       = bufferRegions[0];
		mStaticMesh.posBuffer         = bufferRegions[1];
		mStaticMesh.normalBuffer      = bufferRegions[2];
		//----
		mRectangleData.indexBuffer    = bufferRegions[3];
		mRectangleData.posBuffer      = bufferRegions[4];
		mRectangleData.texCoordBuffer = bufferRegions[5];
		
		mStaticColourBuffer           = bufferRegions[6];
		mParticlesRegion              = bufferRegions[7];
	}
	
	ofPixels pix;
	ofLoadImage( pix, "brighton.png" );

	of::vk::ImageTransferSrcData imgData;
	imgData.pData         = pix.getData();
	imgData.numBytes      = pix.size();
	imgData.extent.width  = pix.getWidth();
	imgData.extent.height = pix.getHeight();

	mImage = stagingContext.storeImageCmd( imgData, mImageAllocator );

	mTexture = std::make_shared<of::vk::Texture>( renderer->getVkDevice(), *mImage );

}

//--------------------------------------------------------------

void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------

void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		const_cast<of::vk::DrawCommand&>( drawPhong ).getPipelineState().touchShader();
		// const_cast<of::vk::DrawCommand&>( drawFullScreenQuad ).getPipelineState().touchShader();
		// const_cast<of::vk::DrawCommand&>( drawTextured ).getPipelineState().touchShader();
	} else if ( key == 'l' ){
		isFrameLocked ^= true;
		ofSetFrameRate( isFrameLocked ? EXAMPLE_TARGET_FRAME_RATE : 0);
		ofLog() << "Framerate " << ( isFrameLocked ? "" : "un" ) << "locked.";
	} else if ( key == 'f' ){
		ofToggleFullscreen();
	}
}

//--------------------------------------------------------------

void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------

void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	mCam.setControlArea( {0,0,float(w),float(h)} );
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

