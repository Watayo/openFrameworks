#include "ofMain.h"
#include "ofApp.h"
#include "vk/ofAppVkNoWindow.h"


//========================================================================
int main(){

	// Do basic initialisation (mostly setup timers, and randseed)
	ofInit();

	auto consoleLogger = new ofConsoleLoggerChannel();
	ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( consoleLogger, []( ofBaseLoggerChannel * lhs){} ) );

	// Create a new window 
	// auto mainWindow = std::make_shared<ofAppGLFWWindow>();
	auto mainWindow = std::make_shared<ofAppVkNoWindow>();

	// Store main window in mainloop
	ofGetMainLoop()->addWindow( mainWindow );
	
	{
		ofVkWindowSettings settings;
		settings.rendererSettings.setVkVersion( 1, 0, 42 );
		settings.rendererSettings.numSwapchainImages = 3;
		settings.rendererSettings.numVirtualFrames = 3;
		settings.rendererSettings.presentMode = ::vk::PresentModeKHR::eMailbox;
		settings.rendererSettings.requestedQueues = {::vk::QueueFlagBits::eGraphics, ::vk::QueueFlagBits::eCompute};

#ifdef NDEBUG
		settings.rendererSettings.useDebugLayers = false;
#else
		settings.rendererSettings.useDebugLayers = true;
#endif

		// Initialise main window, and associated renderer.
		mainWindow->setup( settings );
	}

	// Initialise and start application
	ofRunApp( std::make_shared<ofApp>() );
}
