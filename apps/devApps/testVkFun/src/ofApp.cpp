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

	setupDrawCommand();

	setupMeshL();

	mMeshTeapot = std::make_shared<ofMesh>();
	mMeshTeapot->load( "ico-m.ply" );

	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );

	//mCam.enableMouseInput();
	//mCam.setControlArea( { 0,0,float(ofGetWidth()),float(ofGetHeight() )} );
}

//--------------------------------------------------------------

void ofApp::setupDrawCommand(){
	// shader creation makes shader reflect. 
	auto mShaderDefault = std::shared_ptr<of::vk::Shader>( new of::vk::Shader( renderer->getVkDevice(),
	{
		{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
		{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
	} ) );

	of::vk::GraphicsPipelineState pipeline;

	pipeline.depthStencilState
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE )
		;
	pipeline.inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
	//pipeline.setPolyMode( ::vk::PolygonMode::eLine );
	pipeline.setShader( mShaderDefault );

	const_cast<of::vk::DrawCommand&>(dc).setup( pipeline );
}

//--------------------------------------------------------------

void ofApp::setupMeshL(){
	// Horizontally elongated "L___" shape
	mMeshL = make_shared<ofMesh>();
	vector<glm::vec3> vert{
		{ 0.f,0.f,0.f },
		{ 20.f,20.f,0.f },
		{ 0.f,100.f,0.f },
		{ 20.f,100.f,0.f },
		{ 200.f,0.f,0.f },
		{ 200.f,20.f,0.f }
	};

	vector<ofIndexType> idx{
		0, 1, 2,
		1, 3, 2,
		0, 4, 1,
		1, 4, 5,
	};

	vector<glm::vec3> norm( vert.size(), { 0, 0, 1.f } );

	mMeshL->addVertices( vert );
	mMeshL->addNormals( norm );
	mMeshL->addIndices( idx );
}

//--------------------------------------------------------------

void ofApp::update(){

	// we need to make the camera its matrices and respond to the mouse input 
	// somehow, that's why we use begin/end here
	mCam.begin(); // threre should not need to be need for this!
	mCam.end();	  // threre should not need to be need for this!

	ofSetWindowTitle( ofToString( ofGetFrameRate(), 2, ' ' ) );
}

//--------------------------------------------------------------
void ofApp::draw(){

	auto & currentContext = *renderer->getDefaultContext();

	// first thing we need to add command buffers that deal with
	// copying data. 
	//
	// then issue a pipeline barrier for data copy if we wanted to use static data immediately. 
	// this ensures the barrier is not within a renderpass,
	// as the renderpass will only start with the command buffer that has been created through a batch.
	//
	// if we don't issue a barrier, the transfer will be done when the frame has finished rendering, 
	// as the fence will guarantee that everything enqueued has finished. The challenge here is that 
	// this will be a few frames ahead - depeding on the number of virtual frames.
	//
	// submit copy command buffers 
	// 
	// currentContext.submit( ::vk::CommandBuffer() );
	//
	// in the draw command we can then specify to set the 
	// buffer ID and offset for an attribute to come from the static allocator.

	// Create temporary batch object from default context
	of::vk::RenderBatch batch{ currentContext };

	static const glm::mat4x4 clip( 1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f );

	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );

	ofMatrix4x4 modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 0.f, 1.f } ) );

	// Create a fresh copy of our prototype const draw command
	of::vk::DrawCommand ndc = dc;

	// Update uniforms for draw command
	ndc.setUniform( "projectionMatrix", projectionMatrix );            // | 
	ndc.setUniform( "viewMatrix"      , mCam.getModelViewMatrix() );   // |> set camera matrices
	ndc.setUniform( "modelMatrix"     , modelMatrix );
	ndc.setUniform( "globalColor"     , ofFloatColor::magenta );
	ndc.setMesh( mMeshTeapot );

	// Add draw command to batch 
	batch.draw( ndc );

	// Build vkCommandBuffer inside batch and submit CommandBuffer to 
	// parent context of batch.
	batch.submit();	

	// At end of draw(), context will submit its list of vkCommandBuffers
	// to the graphics queue in one API call.
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		const_cast<of::vk::DrawCommand&>( dc ).getPipelineState().touchShader();
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

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

