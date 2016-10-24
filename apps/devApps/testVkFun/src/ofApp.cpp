#include "ofApp.h"

#define EXAMPLE_TARGET_FRAME_RATE 60
bool isFrameLocked = true;

std::shared_ptr<ofVkRenderer> renderer = nullptr;

//--------------------------------------------------------------

void ofApp::setup(){
	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	ofDisableSetupScreen();
	ofSetFrameRate( EXAMPLE_TARGET_FRAME_RATE );

	setupStaticAllocators();

	setupDrawCommands();

	setupMeshL();

	mMeshPly = std::make_shared<ofMesh>();
	mMeshPly->load( "ico-m.ply" );

	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );

	mCam.setEvents( ofEvents() );
}

//--------------------------------------------------------------

void ofApp::setupStaticAllocators(){

	{
		of::vk::BufferAllocator::Settings allocatorSettings;
		allocatorSettings.device = renderer->getVkDevice();
		allocatorSettings.size = ( 1 << 24UL ); // 16 MB
		allocatorSettings.frameCount = 1;
		allocatorSettings.memFlags = ::vk::MemoryPropertyFlagBits::eDeviceLocal;
		allocatorSettings.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
		allocatorSettings.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();

		mStaticAllocator = std::make_unique<of::vk::BufferAllocator>( allocatorSettings );
		mStaticAllocator->setup();
	}
	{
		of::vk::ImageAllocator::Settings allocatorSettings;
		allocatorSettings.device = renderer->getVkDevice();
		allocatorSettings.size = ( 1 << 24UL ); // 16 MB
		allocatorSettings.memFlags = ::vk::MemoryPropertyFlagBits::eDeviceLocal;
		allocatorSettings.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
		allocatorSettings.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();

		mImageAllocator = std::make_unique<of::vk::ImageAllocator>( allocatorSettings );
		mImageAllocator->setup();
	}
}

//--------------------------------------------------------------

void ofApp::setupDrawCommands(){
	 
	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings.device = renderer->getVkDevice();
		shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex]   = "default.vert";
		shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "default.frag";
		shaderSettings.printDebugInfo = true;

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
		shaderSettings.device = renderer->getVkDevice();
		shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex]   = "fullScreenQuad.vert";
		shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "fullScreenQuad.frag";
		shaderSettings.printDebugInfo = true;

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
		shaderSettings.device = renderer->getVkDevice();
		shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex]   = "textured.vert";
		shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "textured.frag";
		shaderSettings.printDebugInfo = true;

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
	
}

//--------------------------------------------------------------

void ofApp::draw(){

	auto & currentContext = *renderer->getDefaultContext();

	uploadStaticData( currentContext );

	static const glm::mat4x4 clip ( 
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f 
	);

	auto viewMatrix       = mCam.getModelViewMatrix();
	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );


	// Create a fresh copy of our prototype const draw command
	ofMatrix4x4 modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 1.f, 0.f } ) );

	auto hero = drawPhong;
	hero
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", modelMatrix )
		//.setUniform( "globalColor", ofFloatColor::magenta )
		.setNumIndices( mStaticMesh.indexBuffer.numElements )
		.setIndices( mStaticMesh.indexBuffer )
		.setAttribute( 0, mStaticMesh.posBuffer )
		.setAttribute( 1, mStaticMesh.normalBuffer )
		;

	auto texturedRect = drawTextured;
	texturedRect
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", glm::mat4() )
		.setUniform( "tex_0", mTexture )
		.setNumIndices( mRectangleData.indexBuffer.numElements )
		.setIndices( mRectangleData.indexBuffer )
		.setAttribute( 0, mRectangleData.posBuffer )
		.setAttribute( 1, mRectangleData.texCoordBuffer )
		;

	of::vk::RenderBatch batch{ currentContext };

	batch.draw( drawFullScreenQuad );
	batch.draw( hero );
	batch.draw( texturedRect );

	batch.submit();	

	// At end of draw(), context will submit its list of vkCommandBuffers
	// to the graphics queue in one API call.
}

//--------------------------------------------------------------

void ofApp::uploadStaticData( of::vk::RenderContext & currentContext ){

	static bool wasUploaded = false;

	if ( wasUploaded ){
		return;
	}

	ofMesh meshPlane = ofMesh::plane( 1024 / 2, 768 / 2, 2, 2, OF_PRIMITIVE_TRIANGLES );

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
		}
	};

	const auto & staticBuffer = mStaticAllocator->getBuffer();

	std::vector<of::vk::BufferRegion> bufferRegions = currentContext.storeBufferDataCmd( srcDataVec, mStaticAllocator );

	if ( bufferRegions.size() == 6 ) {
		mStaticMesh.indexBuffer       = bufferRegions[0];
		mStaticMesh.posBuffer         = bufferRegions[1];
		mStaticMesh.normalBuffer      = bufferRegions[2];
		//----
		mRectangleData.indexBuffer    = bufferRegions[3];
		mRectangleData.posBuffer      = bufferRegions[4];
		mRectangleData.texCoordBuffer = bufferRegions[5];
	}
	
	ofPixels pix;
	ofLoadImage( pix, "brighton.png" );

	of::vk::ImageTransferSrcData imgData;
	imgData.pData         = pix.getData();
	imgData.numBytes      = pix.size();
	imgData.extent.width  = pix.getWidth();
	imgData.extent.height = pix.getHeight();

	mImage = currentContext.storeImageCmd( imgData, mImageAllocator );

	mTexture = std::make_shared<of::vk::Texture>( renderer->getVkDevice(), *mImage );

	wasUploaded = true;
}

//--------------------------------------------------------------

void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------

void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		//const_cast<of::vk::DrawCommand&>( drawPhong ).getPipelineState().touchShader();
		//const_cast<of::vk::DrawCommand&>( drawFullScreenQuad ).getPipelineState().touchShader();
		const_cast<of::vk::DrawCommand&>( drawTextured ).getPipelineState().touchShader();
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

