#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"
#include "vk/DrawCommand.h"

/*

MISSION: 

	A RenderContext needs to be able to live within its own thread - 
	A RenderContext needs to have its own pools, 
	and needs to be thread-safe.

	One or more batches may submit into a rendercontext - the render-
	context will accumulate vkCommandbuffers, and will submit them 
	on submitDraw.

*/

class ofVkRenderer; // ffdecl.

namespace of{
namespace vk{

class TransferBatch; //ffdecl.
class RenderBatch;

// ------------------------------------------------------------

class RenderContext
{
	friend RenderBatch;
public:
	struct Settings
	{
		ofVkRenderer *                         renderer = nullptr;
		Allocator::Settings                    transientMemoryAllocatorSettings;
		std::shared_ptr<::vk::PipelineCache>   pipelineCache;
		::vk::Rect2D                           renderArea;
	};
private:

	const Settings mSettings;
	const ::vk::Device&                         mDevice = mSettings.transientMemoryAllocatorSettings.device;

	struct VirtualFrame
	{
		::vk::CommandPool                       commandPool;
		::vk::QueryPool                         queryPool;
		::vk::Framebuffer                       frameBuffer;
		std::list<::vk::DescriptorPool>         descriptorPools;
		std::map<uint64_t, ::vk::DescriptorSet> descriptorSetCache;
		::vk::Semaphore                         semaphoreImageAcquired;
		::vk::Semaphore                         semaphoreRenderComplete;
		::vk::Fence                             fence;
		std::shared_ptr<TransferBatch>          transferBatch;
		std::vector<::vk::CommandBuffer>        commandBuffers;
	};

	// Max number of descriptors per type
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mDescriptorPoolSizes;

	// Number of descriptors left available for allocation from mDescriptorPool.
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mAvailableDescriptorCounts;

	// Max number of sets which can be allocated from the main per-frame descriptor pool
	uint32_t mDescriptorPoolMaxSets = 0;

	// Bitfield indicating whether the descriptor pool for a virtual frame is dirty 
	// Each bit represents a virtual frame index. 
	// We're not expecting more than 64 virtual frames (more than 3 seldom make sense)
	uint64_t mDescriptorPoolsDirty = 0; // -1 == all bits '1' == all dirty

	std::vector<VirtualFrame>          mVirtualFrames;
	std::unique_ptr<of::vk::Allocator> mTransientMemory;

	size_t                             mCurrentVirtualFrame = 0;
	const ::vk::Rect2D&                mRenderArea = mSettings.renderArea;

	// cache for all pipelines ever used within this context
	std::map<uint64_t, std::shared_ptr<::vk::Pipeline>>    mPipelineCache;

	// Fetch descriptor either from cache - or allocate and initialise a descriptor based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const DrawCommand & drawCommand );

	// Re-consolidate descriptor pools if necessary
	void updateDescriptorPool();
	
	void resetFence();

public:

	RenderContext( const Settings& settings );
	~RenderContext(){
		for ( auto & vf : mVirtualFrames ){
			if ( vf.commandPool ){
				mDevice.destroyCommandPool( vf.commandPool );
			}
			for ( auto & pool : vf.descriptorPools ){
				mDevice.destroyDescriptorPool( pool );
			}
			if ( vf.semaphoreImageAcquired ){
				mDevice.destroySemaphore( vf.semaphoreImageAcquired );
			}
			if ( vf.semaphoreRenderComplete ){
				mDevice.destroySemaphore( vf.semaphoreRenderComplete );
			}
			if ( vf.fence ){
				mDevice.destroyFence( vf.fence );
			}
			if ( vf.frameBuffer ){
				mDevice.destroyFramebuffer( vf.frameBuffer );
			}
		}
		mVirtualFrames.clear();
		mTransientMemory->reset();
	};

	const ::vk::CommandPool & getCommandPool() const;

	::vk::Fence & getFence();
	::vk::Semaphore & getImageAcquiredSemaphore();
	::vk::Semaphore & getSemaphoreRenderComplete();
	::vk::Framebuffer & getFramebuffer();

	std::shared_ptr<::vk::Pipeline>& borrowPipeline( uint64_t pipelineHash ){
		return mPipelineCache[pipelineHash];
	};

	const ::vk::Device & getDevice() const{
		return mDevice;
	};

	void setRenderArea( const ::vk::Rect2D& renderArea );
	const ::vk::Rect2D & getRenderArea() const;
	const std::unique_ptr<Allocator> & RenderContext::getAllocator();


	void setup();
	void begin();
	// move command buffer to the rendercontext for batched submission
	void submit( ::vk::CommandBuffer&& commandBuffer );
	void submitDraw();
	// void submitTransfer();
	void swap();

};

// ------------------------------------------------------------

inline void RenderContext::submit(::vk::CommandBuffer && commandBuffer) {
	mVirtualFrames.at( mCurrentVirtualFrame ).commandBuffers.emplace_back(std::move(commandBuffer));
}


inline ::vk::Fence & RenderContext::getFence(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).fence;
}

inline ::vk::Semaphore & RenderContext::getImageAcquiredSemaphore(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreImageAcquired;
}

inline ::vk::Semaphore & RenderContext::getSemaphoreRenderComplete(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreRenderComplete;
}

inline ::vk::Framebuffer & RenderContext::getFramebuffer(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).frameBuffer;
}


inline void RenderContext::setRenderArea( const::vk::Rect2D & renderArea_ ){
	const_cast<::vk::Rect2D&>( mSettings.renderArea ) = renderArea_;
}

inline const ::vk::Rect2D & RenderContext::getRenderArea() const{
	return mRenderArea;
}

inline const std::unique_ptr<Allocator> & RenderContext::getAllocator(){
	return mTransientMemory;
}

}  // end namespace of::vk
}  // end namespace of