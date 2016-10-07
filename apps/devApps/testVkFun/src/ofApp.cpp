#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

#define EXAMPLE_TARGET_FRAME_RATE 60
bool isFrameLocked = true;

std::shared_ptr<ofVkRenderer> renderer = nullptr;


//--------------------------------------------------------------

void ofApp::setup(){
	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	ofDisableSetupScreen();
	ofSetFrameRate( EXAMPLE_TARGET_FRAME_RATE );

	setupStaticAllocator();

	setupDrawCommand();

	setupMeshL();

	mMeshPly = std::make_shared<ofMesh>();
	mMeshPly->load( "ico-m.ply" );
	//mMeshPly->load( "teapot.ply" );

	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );

	mCam.setEvents( ofEvents() );
}

//--------------------------------------------------------------

void ofApp::setupStaticAllocator(){
	of::vk::Allocator::Settings allocatorSettings;
	allocatorSettings.device = renderer->getVkDevice();
	allocatorSettings.size = ( 1 << 24UL ); // 16 MB
	allocatorSettings.frameCount = 1;
	allocatorSettings.memFlags = ::vk::MemoryPropertyFlagBits::eDeviceLocal;
	allocatorSettings.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
	allocatorSettings.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();
	
	mStaticAllocator = std::make_unique<of::vk::Allocator>( allocatorSettings );
	mStaticAllocator->setup();
}

//--------------------------------------------------------------

void ofApp::setupDrawCommand(){
	 
	of::vk::Shader::Settings shaderSettings;
	shaderSettings.device = renderer->getVkDevice();
	shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex  ] = "default.vert";
	shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "default.frag";

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
		//.setSrcAlphaBlendFactor

	const_cast<of::vk::DrawCommand&>(drawPhong).setup( pipeline );

	// ------ 

	shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex]   = "fullScreenQuad.vert";
	shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "fullScreenQuad.frag";
	auto mShaderFullScreenQuad = std::make_shared<of::vk::Shader>( shaderSettings );
	
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

//--------------------------------------------------------------

void ofApp::update(){

	ofSetWindowTitle( ofToString( ofGetFrameRate(), 2, ' ' ) );
	
}

//--------------------------------------------------------------

void ofApp::draw(){

	auto & currentContext = *renderer->getDefaultContext();

	uploadStaticAttributes( currentContext );

	// Create temporary batch object from default context
	of::vk::RenderBatch batch{ currentContext };

	static const glm::mat4x4 clip( 
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f 
	);

	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );

	ofMatrix4x4 modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 1.f, 0.f } ) );

	// Create a fresh copy of our prototype const draw command
	of::vk::DrawCommand drawObject = drawPhong;

	drawObject.setUniform( "projectionMatrix", projectionMatrix );            // | 
	drawObject.setUniform( "viewMatrix"      , mCam.getModelViewMatrix() );   // |> set camera matrices
	drawObject.setUniform( "modelMatrix"     , modelMatrix );
	drawObject.setUniform( "globalColor"     , ofFloatColor::magenta );
	// ndc.setMesh( mMeshTeapot );
	
	drawObject
		.setNumIndices( mStaticMesh.indexBuffer.numElements )
		.setIndices( mStaticMesh.indexBuffer.buffer, mStaticMesh.indexBuffer.offset )
		.setAttribute( 0, mStaticMesh.posBuffer.buffer, mStaticMesh.posBuffer.offset )
		.setAttribute( 1, mStaticMesh.normalBuffer.buffer, mStaticMesh.normalBuffer.offset )
		;

	batch.draw( drawObject );
	batch.draw( drawFullScreenQuad );

	// Build vkCommandBuffer inside batch and submit CommandBuffer to 
	// parent context of batch.
	batch.submit();	

	// At end of draw(), context will submit its list of vkCommandBuffers
	// to the graphics queue in one API call.
}

//--------------------------------------------------------------

void ofApp::uploadStaticAttributes( of::vk::RenderContext & currentContext ){

	static bool wasUploaded = false;

	if ( wasUploaded ){
		return;
	}

	std::vector<of::vk::TransferSrcData> srcDataVec = {
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
	};

	const auto & staticBuffer = mStaticAllocator->getBuffer();

	std::vector<of::vk::BufferRegion> bufferRegions = currentContext.storeDataCmd( srcDataVec, mStaticAllocator );

	if ( bufferRegions.size() == 3 ) {
		mStaticMesh.indexBuffer  = bufferRegions[0];
		mStaticMesh.posBuffer    = bufferRegions[1];
		mStaticMesh.normalBuffer = bufferRegions[2];
	}
	
	wasUploaded = true;
}

//--------------------------------------------------------------

void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------

void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		const_cast<of::vk::DrawCommand&>( drawPhong ).getPipelineState().touchShader();
		const_cast<of::vk::DrawCommand&>( drawFullScreenQuad ).getPipelineState().touchShader();
	} else if ( key == 'l' ){
		isFrameLocked ^= true;
		ofSetFrameRate( isFrameLocked ? EXAMPLE_TARGET_FRAME_RATE : 0);
		ofLog() << "Framerate " << ( isFrameLocked ? "" : "un" ) << "locked.";
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

