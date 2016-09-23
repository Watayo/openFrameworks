#include "vk/RenderContext.h"

// ------------------------------------------------------------
of::RenderContext::RenderContext( const Settings & settings )
	: mSettings( settings ){
	mTransientMemory = std::make_unique<of::vk::Allocator>( settings.transientMemoryAllocatorSettings );
	mVirtualFrames.resize( mSettings.transientMemoryAllocatorSettings.frameCount );
	mDescriptorPoolSizes.fill( 0 );
	mAvailableDescriptorCounts.fill( 0 );
}

// ------------------------------------------------------------

::vk::CommandPool & of::RenderContext::getCommandPool(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).commandPool;
}


// ------------------------------------------------------------

void of::RenderContext::setup(){
	for ( auto &f : mVirtualFrames ){
		f.semaphoreImageAcquired = mDevice.createSemaphore( {} );
		f.semaphoreRenderComplete = mDevice.createSemaphore( {} );
		f.fence = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );	/* Fence starts as "signaled" */
		f.commandPool = mDevice.createCommandPool( { ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer } );
	}
	mTransientMemory->setup();
}

// ------------------------------------------------------------

void of::RenderContext::begin(){
	mTransientMemory->free();
	// re-create descriptor pool for current virtual frame if necessary
	updateDescriptorPool();
}

// ------------------------------------------------------------

void of::RenderContext::swap(){
	mCurrentVirtualFrame = ( mCurrentVirtualFrame + 1 ) % mSettings.transientMemoryAllocatorSettings.frameCount;
	mTransientMemory->swap();
}

// ------------------------------------------------------------

const::vk::DescriptorSet of::RenderContext::getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const of::DrawCommand & drawCommand ){

	auto & currentVirtualFrame = mVirtualFrames[mCurrentVirtualFrame];
	auto & descriptorSetCache = currentVirtualFrame.descriptorSetCache;

	auto cachedDescriptorSetIt = descriptorSetCache.find( descriptorSetHash );

	if ( cachedDescriptorSetIt != descriptorSetCache.end() ){
		return cachedDescriptorSetIt->second;
	}

	// ----------| Invariant: descriptor set has not been found in the cache for the current frame.

	::vk::DescriptorSet allocatedDescriptorSet = nullptr;

	auto & descriptors = drawCommand.getDescriptorSetData( setId ).descriptorBindings;

	// find out required pool sizes for this descriptor set

	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> requiredPoolSizes;
	requiredPoolSizes.fill( 0 );

	for ( const auto & d : descriptors ){
		uint32_t arrayIndex = uint32_t( d.type );
		++requiredPoolSizes[arrayIndex];
	}


	// First, we have to figure out if the current descriptor pool has enough space available 
	// over all descriptor types to allocate the desciptors needed to fill the desciptor set requested.


	// perform lexicographical compare, i.e. compare pairs of corresponding elements 
	// until the first mismatch 
	bool poolLargeEnough = ( mAvailableDescriptorCounts >= requiredPoolSizes );

	if ( poolLargeEnough == false ){

		// Allocation cannot be made using current descriptorPool (we're out of descriptors)
		//
		// Allocate a new descriptorpool - and make sure there is enough space to contain
		// all new descriptors.

		std::vector<::vk::DescriptorPoolSize> descriptorPoolSizes;
		descriptorPoolSizes.reserve( requiredPoolSizes.size() );
		for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
			if ( requiredPoolSizes[i] != 0 ){
				descriptorPoolSizes.emplace_back( ::vk::DescriptorType( i ), requiredPoolSizes[i] );
			}
		}

		::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
		descriptorPoolCreateInfo
			.setFlags( ::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet )
			.setMaxSets( 1 )
			.setPoolSizeCount( descriptorPoolSizes.size() )
			.setPPoolSizes( descriptorPoolSizes.data() )
			;

		auto descriptorPool = mDevice.createDescriptorPool( descriptorPoolCreateInfo );

		mVirtualFrames[mCurrentVirtualFrame].descriptorPools.push_back( descriptorPool );

		// this means all descriptor pools are dirty and will have to be re-created with 
		// more space to accomodate more descriptor sets.
		mDescriptorPoolsDirty = -1;

		for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
			mDescriptorPoolSizes[i] += requiredPoolSizes[i];
			// Update number of available descriptors from descriptor pool. 
			mAvailableDescriptorCounts[i] += requiredPoolSizes[i];
		}

		// Increase maximum number of sets for allocation from descriptor pool
		mDescriptorPoolMaxSets += 1;

	}

	// ---------| invariant: currentVirtualFrame.descriptorPools.back() contains a pool large enough to allocate our descriptor set from

	// we are able to allocate from the current descriptor pool
	auto & setLayout = *( drawCommand.getInfo().pipeline.getShader()->getDescriptorSetLayout( setId ) );
	auto allocInfo = ::vk::DescriptorSetAllocateInfo();
	allocInfo
		.setDescriptorPool( currentVirtualFrame.descriptorPools.back() )
		.setDescriptorSetCount( 1 )
		.setPSetLayouts( &setLayout )
		;

	allocatedDescriptorSet = mDevice.allocateDescriptorSets( allocInfo ).front();

	// decrease number of available descriptors from the pool 
	for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
		mAvailableDescriptorCounts[i] -= requiredPoolSizes[i];
	}

	// Once desciptor sets have been allocated, we need to write to them using write desciptorset
	// to initialise them.

	std::vector<::vk::WriteDescriptorSet> writeDescriptorSets;

	writeDescriptorSets.reserve( descriptors.size() );

	for ( const auto & d : descriptors ){

		// ( we cast address to sampler, as the layout of DescriptorData_t::sampler and
		// DescriptorData_t::imageLayout is the same as if we had 
		// a "proper" ::vk::descriptorImageInfo )
		const ::vk::DescriptorImageInfo* descriptorImageInfo = reinterpret_cast<const ::vk::DescriptorImageInfo*>( &d.sampler );
		// same with bufferInfo
		const ::vk::DescriptorBufferInfo* descriptorBufferInfo = reinterpret_cast<const ::vk::DescriptorBufferInfo*>( &d.buffer );

		writeDescriptorSets.emplace_back(
			allocatedDescriptorSet,         // dstSet
			d.bindingNumber,                // dstBinding
			d.arrayIndex,                   // dstArrayElement
			1,                              // descriptorCount
			d.type,                         // descriptorType
			descriptorImageInfo,            // pImageInfo 
			descriptorBufferInfo,           // pBufferInfo
			nullptr                         // 
		);
	}

	mDevice.updateDescriptorSets( writeDescriptorSets, nullptr );

	// Now store the newly allocated descriptor set in this frame's descriptor set cache
	// so it may be re-used.
	descriptorSetCache[descriptorSetHash] = allocatedDescriptorSet;

	return allocatedDescriptorSet;
}

// ------------------------------------------------------------

void of::RenderContext::updateDescriptorPool(){

	// If current virtual frame descriptorpool is dirty,
	// re-allocate frame descriptorpool based on total number
	// of descriptorsets enumerated in mDescriptorPoolSizes
	// and mDescriptorPoolMaxsets.

	if ( 0 == ( ( 1ULL << mCurrentVirtualFrame ) & mDescriptorPoolsDirty ) ){
		return;
	}

	// --------| invariant: Descriptor Pool for the current virtual frame is dirty.

	// Destroy all cached descriptorSets for the current virtual frame, if any
	mVirtualFrames[mCurrentVirtualFrame].descriptorSetCache.clear();

	// Destroy all descriptor pools for the current virtual frame, if any.
	// This will free any descriptorSets allocated from these pools.
	for ( const auto& d : mVirtualFrames[mCurrentVirtualFrame].descriptorPools ){
		mDevice.destroyDescriptorPool( d );
	}
	mVirtualFrames[mCurrentVirtualFrame].descriptorPools.clear();

	// Re-create descriptor pool for current virtual frame
	// based on number of max descriptor pool count

	std::vector<::vk::DescriptorPoolSize> descriptorPoolSizes;
	descriptorPoolSizes.reserve( VK_DESCRIPTOR_TYPE_RANGE_SIZE );
	for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
		if ( mDescriptorPoolSizes[i] != 0 ){
			descriptorPoolSizes.emplace_back( ::vk::DescriptorType( i ), mDescriptorPoolSizes[i] );
		}
	}

	if ( descriptorPoolSizes.empty() ){
		return;
		//!TODO: this needs a fix: happens when method is called for the very first time
		// with no pool sizes known.
	}

	::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
	descriptorPoolCreateInfo
		.setMaxSets( mDescriptorPoolMaxSets )
		.setPoolSizeCount( descriptorPoolSizes.size() )
		.setPPoolSizes( descriptorPoolSizes.data() )
		;

	mVirtualFrames[mCurrentVirtualFrame].descriptorPools.emplace_back( mDevice.createDescriptorPool( descriptorPoolCreateInfo ) );

	// Reset number of available descriptors for allocation from 
	// main descriptor pool
	mAvailableDescriptorCounts = mDescriptorPoolSizes;

	// Mark descriptor pool for this frame as not dirty
	mDescriptorPoolsDirty ^= ( 1ULL << mCurrentVirtualFrame );

}