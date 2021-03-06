#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace of{
namespace vk{


class AbstractAllocator
{

public:
	
	struct Settings
	{
		~Settings() {} // force vtable;
		::vk::PhysicalDeviceProperties       physicalDeviceProperties;
		::vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
		::vk::Device                         device     = nullptr;
		::vk::DeviceSize                     size       = 0; // how much memory to reserve on hardware for this allocator
		::vk::MemoryPropertyFlags            memFlags = ( ::vk::MemoryPropertyFlagBits::eHostVisible | ::vk::MemoryPropertyFlagBits::eHostCoherent );
		std::vector<uint32_t>                queueFamilyIndices;
	};

	virtual ~AbstractAllocator() {}; // force vtable

	virtual void reset() = 0;
	virtual bool allocate(::vk::DeviceSize byteCount_, ::vk::DeviceSize& offset) = 0;
	virtual void swap() = 0;
	virtual const ::vk::DeviceMemory& getDeviceMemory() const = 0;
	virtual const AbstractAllocator::Settings& getSettings() const = 0;
	
};

// ----------------------------------------------------------------------
namespace {

	inline bool getMemoryAllocationInfo( 
		const ::vk::PhysicalDeviceMemoryProperties& memProps, 
		const::vk::MemoryRequirements & memReqs, 
		::vk::MemoryPropertyFlags memFlags, 
		::vk::MemoryAllocateInfo & allocInfo 
	){
		if ( !memReqs.size ){
			allocInfo.allocationSize = 0;
			allocInfo.memoryTypeIndex = ~0;
			return true;
		}

		// Find an available memory type that satifies the requested properties.
		uint32_t memoryTypeIndex;
		for ( memoryTypeIndex = 0; memoryTypeIndex < memProps.memoryTypeCount; ++memoryTypeIndex ){
			if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
				( memProps.memoryTypes[memoryTypeIndex].propertyFlags & memFlags ) == memFlags ){
				break;
			}
		}
		if ( memoryTypeIndex >= memProps.memoryTypeCount ){
			assert( 0 && "memorytypeindex not found" );
			return false;
		}

		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		return true;
	}

} // end anonymous namespace


} // namespace of::vk
} // namespace of
