#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "vk/BufferAllocator.h"
#include "vk/ImageAllocator.h"
#include "vk/HelperTypes.h"
#include "vk/ComputeCommand.h"
/*

A Context acts as an accumulator and owner for Renderbatches
it safely manages memory by accumulating commandBuffers and their
dependent data in virtual frames. 

It abstracts the swapchain.

MISSION: 

	A Context needs to be able to live within its own thread - 
	A Context needs to have its own pools, 
	and needs to be thread-safe.

	One or more batches may submit into a rendercontext - the render-
	context will accumulate vkCommandbuffers, and will submit them 
	on submitToQueue.

	A Context is the OWNER of all resources used to draw within 
	one thread.


*/

class ofVkRenderer; // ffdecl.

namespace of{
namespace vk{

class RenderBatch; // ffdecl.
class ComputeCommand;

// ------------------------------------------------------------

class Context
{
	friend RenderBatch;
	friend ComputeCommand;
public:
	struct Settings
	{
		ofVkRenderer *                         renderer = nullptr;
		BufferAllocator::Settings              transientMemoryAllocatorSettings;
		std::shared_ptr<::vk::PipelineCache>   pipelineCache;
		::vk::RenderPass                       renderPass;  // owning
		std::vector<::vk::ClearValue>          renderPassClearValues; 
		::vk::Rect2D                           renderArea;
		bool                                   renderToSwapChain = false; // whether this rendercontext renders to swapchain
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
		::vk::Semaphore                         semaphoreWait;   // only used if renderContext renders to swapchain
		::vk::Semaphore                         semaphoreSignalOnComplete; // semaphore will signal when work complete
		std::vector<::vk::CommandBuffer>        commandBuffers;

		// The most important element in here is the fence, as it protects 
		// all resources above from being overwritten while still in flight.
		// The fence is placed in the command stream upon queue submit, and 
		// it is waited upon in begin(). This ensures all resources for 
		// this virtual frame are available and the GPU is finished using 
		// them for rendering / presenting.
		::vk::Fence                             fence;
	};

	std::vector<VirtualFrame>                   mVirtualFrames;
	size_t                                      mCurrentVirtualFrame = 0;
	
	// Renderpass with subpasses for this context. from which framebuffers are derived.
	// each context has their own renderpass object, from which framebuffers are partly derived.
	const ::vk::RenderPass &                    mRenderPass = mSettings.renderPass; 
	uint32_t                                    mSubpassId  = 0;

	std::unique_ptr<of::vk::BufferAllocator>    mTransientMemory;

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

	// Re-consolidate descriptor pools if necessary
	void updateDescriptorPool();

	// Fetch descriptor either from cache - or allocate and initialise a descriptor based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const ::vk::DescriptorSetLayout & setLayout_, const std::vector<of::vk::DescriptorSetData_t::DescriptorData_t> & descriptors );

	// cache for all pipelines ever used within this context
	std::map<uint64_t, std::shared_ptr<::vk::Pipeline>>    mPipelineCache;
	
	const ::vk::Rect2D&                mRenderArea = mSettings.renderArea;
	
	void waitForFence();

	std::shared_ptr<::vk::Pipeline>& borrowPipeline( uint64_t pipelineHash ){
		return mPipelineCache[pipelineHash];
	};
	
	const std::unique_ptr<BufferAllocator> & getAllocator() const;
	
	// move to next virtual frame - called internally in begin() after fence has been cleared.
	void swap();

public:

	Context( const Settings&& settings );
	~Context();

	const ::vk::Fence       & getFence() const ;
	const ::vk::Semaphore   & getSemaphoreWait() const ;
	const ::vk::Semaphore   & getSemaphoreSignalOnComplete() const ;
	const ::vk::Framebuffer & getFramebuffer() const;
	const ::vk::RenderPass  & getRenderPass() const; 
	const size_t              getNumVirtualFrames() const;

	const uint32_t            getSubpassId() const;

	void setupFrameBufferAttachments( const std::vector<::vk::ImageView> &attachments);

	// Stages data for copying into targetAllocator's address space
	// allocates identical memory chunk in local transient allocator and in targetAllocator
	// use BufferCopy vec and a vkCmdBufferCopy to execute copy instruction using a command buffer.
	::vk::BufferCopy stageBufferData( const TransferSrcData& data, const unique_ptr<BufferAllocator> &targetAllocator );
	
	std::vector<::vk::BufferCopy> stageBufferData( const std::vector<TransferSrcData>& dataVec, const unique_ptr<BufferAllocator> &targetAllocator );

	std::vector<BufferRegion> storeBufferDataCmd( const std::vector<TransferSrcData>& dataVec, const unique_ptr<BufferAllocator> &targetAllocator );

	std::shared_ptr<::vk::Image> storeImageCmd( const ImageTransferSrcData& data, const unique_ptr<ImageAllocator>& targetImageAllocator );

	// Create and return command buffer. 
	// Lifetime is limited to current frame. 
	// It *must* be submitted to this context within the same frame, that is, before swap().
	// command buffer will also begin renderpass, based on current framebuffer and render area,
	// and clear the render area based on current clear values.
	::vk::CommandBuffer Context::allocateCommandBuffer(const ::vk::CommandBufferLevel & commandBufferLevel = ::vk::CommandBufferLevel::ePrimary ) const;

	const std::unique_ptr<of::vk::BufferAllocator>& getTransientAllocator() const{
		return mTransientMemory;
	};

	const ::vk::Device & getDevice() const{
		return mDevice;
	};

	void setRenderArea( const ::vk::Rect2D& renderArea );
	const ::vk::Rect2D & getRenderArea() const;

	void setup();
	void begin();
	
	// move command buffer to the rendercontext for batched submission
	void submit( ::vk::CommandBuffer&& commandBuffer );
	
	// submit all accumulated command buffers to vulkan draw queue for rendering
	// this is where semaphore synchronisation happens. 
	void submitToQueue();

	const std::vector<::vk::ClearValue> & getClearValues() const;

	// context which must be waited upon before this context can render
	Context* mSourceContext = nullptr ;

	// define this context to be dependent on another context to be finished rendering first
	void addContextDependency( Context* ctx );

};

// ------------------------------------------------------------

inline void Context::submit(::vk::CommandBuffer && commandBuffer) {
	mVirtualFrames.at( mCurrentVirtualFrame ).commandBuffers.emplace_back(std::move(commandBuffer));
}


inline const ::vk::Fence & Context::getFence() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).fence;
}

inline const ::vk::Semaphore & Context::getSemaphoreWait() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreWait;
}

inline const ::vk::Semaphore & Context::getSemaphoreSignalOnComplete() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreSignalOnComplete;
}

inline const ::vk::Framebuffer & Context::getFramebuffer() const{
	return mVirtualFrames[ mCurrentVirtualFrame ].frameBuffer;
}

inline const ::vk::RenderPass & Context::getRenderPass() const{
	return mSettings.renderPass;
}

inline const size_t Context::getNumVirtualFrames() const{
	return mVirtualFrames.size();
}

inline const uint32_t Context::getSubpassId() const{
	return mSubpassId;
}

inline void Context::setRenderArea( const::vk::Rect2D & renderArea_ ){
	const_cast<::vk::Rect2D&>( mSettings.renderArea ) = renderArea_;
}

inline const ::vk::Rect2D & Context::getRenderArea() const{
	return mRenderArea;
}

inline const std::unique_ptr<BufferAllocator> & Context::getAllocator() const{
	return mTransientMemory;
}

inline const std::vector<::vk::ClearValue>& Context::getClearValues() const{
	return mSettings.renderPassClearValues;
}


// ------------------------------------------------------------

inline std::vector<::vk::BufferCopy> Context::stageBufferData( const std::vector<TransferSrcData>& dataVec, const unique_ptr<BufferAllocator>& targetAllocator )
{
	std::vector<::vk::BufferCopy> regions;
	regions.reserve( dataVec.size());
	
	for (const auto & data : dataVec ){
		regions.push_back(stageBufferData( data, targetAllocator ));
	}

	return regions;
}

// ------------------------------------------------------------

inline ::vk::BufferCopy Context::stageBufferData( const TransferSrcData& data, const unique_ptr<BufferAllocator>& targetAllocator ){
	::vk::BufferCopy region{ 0, 0, 0 };

	region.size = data.numBytesPerElement * data.numElements;

	void * pData;
	if ( targetAllocator->allocate( region.size, region.dstOffset )
		&& mTransientMemory->allocate( region.size, region.srcOffset )
		&& mTransientMemory->map( pData )
		){

		memcpy( pData, data.pData, region.size );

	} else{
		ofLogError() << "StageData: alloc error";
	}
	return region;
}

// ------------------------------------------------------------

inline ::vk::CommandBuffer Context::allocateCommandBuffer (
	const ::vk::CommandBufferLevel & commandBufferLevel) const {
	::vk::CommandBuffer cmd;

	::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo
		.setCommandPool( mVirtualFrames[mCurrentVirtualFrame].commandPool )
		.setLevel( commandBufferLevel )
		.setCommandBufferCount( 1 )
		;

	mDevice.allocateCommandBuffers( &commandBufferAllocateInfo, &cmd );

	return cmd;
}

}  // end namespace of::vk
}  // end namespace of
