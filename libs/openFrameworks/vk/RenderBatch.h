#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"
#include "vk/DrawCommand.h"
#include "vk/RenderContext.h"

namespace of {
namespace vk{

// ------------------------------------------------------------

class RenderBatch
{
	/*

	Batch is an object which processes draw instructions
	received through draw command objects.

	Batch's mission is to create a command buffer where
	the number of pipeline changes is minimal.

	*/
	RenderContext * mRenderContext;

	RenderBatch() = delete;

public:

	RenderBatch( RenderContext& rpc );

	~RenderBatch(){
		// todo: check if batch was submitted already - if not, submit.
		// submit();
	}

private:

	// current draw state for building command buffer
	std::unique_ptr<GraphicsPipelineState>                           mCurrentPipelineState;
	std::unordered_map<uint64_t, std::shared_ptr<::vk::Pipeline>>    mPipelineCache;

	uint32_t            mVkSubPassId = 0;
	::vk::CommandBuffer mVkCmd;

	::vk::RenderPass    mVkRenderPass;  // current renderpass

	std::list<DrawCommand> mDrawCommands;

	void processDrawCommands();


	void beginRenderPass( const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_ );
	void endRenderPass();
	void beginCommandBuffer();
	void endCommandBuffer();

public:

	// !TODO: submit to context - 
	void submit();

	uint32_t nextSubPass();

	void draw( const DrawCommand& dc );
};

// ----------------------------------------------------------------------




inline void RenderBatch::beginRenderPass( const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_ ){

	//ofLog() << "begin renderpass";

	mVkSubPassId = 0;

	if ( mVkRenderPass ){
		ofLogError() << "cannot begin renderpass whilst renderpass already open.";
		return;
	}

	mVkRenderPass = vkRenderPass_;

	//!TODO: get correct clear values, and clear value count
	std::array<::vk::ClearValue, 2> clearValues;
	clearValues[0].setColor( reinterpret_cast<const ::vk::ClearColorValue&>( ofFloatColor::blueSteel ) );
	clearValues[1].setDepthStencil( { 1.f, 0 } );

	::vk::RenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo
		.setRenderPass( vkRenderPass_ )
		.setFramebuffer( vkFramebuffer_ )
		.setRenderArea( renderArea_ )
		.setClearValueCount( clearValues.size() )
		.setPClearValues( clearValues.data() )
		;

	mVkCmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
}

// ----------------------------------------------------------------------
// Inside of a renderpass, draw commands may be sorted, to minimize pipeline and binding swaps.
// so endRenderPass should be the point at which the commands are recorded into the command buffer
// If the renderpass allows re-ordering.
inline uint32_t RenderBatch::nextSubPass(){
	return ++mVkSubPassId;
}

// ----------------------------------------------------------------------

inline void RenderBatch::endRenderPass(){
	// TODO: consolidate/re-order draw commands if buffered
	//ofLog() << "end   renderpass";
	mVkCmd.endRenderPass();
}

// ----------------------------------------------------------------------

inline void RenderBatch::beginCommandBuffer(){
	//ofLog() << "begin command buffer";


	if ( !mVkCmd ){
		::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
		commandBufferAllocateInfo
			.setCommandPool( mRenderContext->getCommandPool() )
			.setLevel( ::vk::CommandBufferLevel::ePrimary )
			.setCommandBufferCount( 1 )
			;
		mVkCmd = ( mRenderContext->getDevice().allocateCommandBuffers( commandBufferAllocateInfo ) ).front();
	}

	mVkCmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

}

// ----------------------------------------------------------------------

inline void RenderBatch::endCommandBuffer(){
	//ofLog() << "end   command buffer";
	mVkCmd.end();
}



// ----------------------------------------------------------------------

} // end namespce of::vk
} // end namespace of