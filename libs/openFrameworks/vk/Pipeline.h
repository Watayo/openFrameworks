#pragma once

#include "vulkan/vulkan.hpp"
#include <string>
#include <array>

#include "ofFileUtils.h"
#include "ofLog.h"

/*

A pipeline is a monolithic compiled object that represents 
all the programmable, and non-dynamic state affecting a 
draw call. 

You can look at it as a GPU program combining shader machine 
code with gpu-hardware-specific machine code dealing with 
blending, primitive assembly, etc. 

The Pipeline has a layout, that's the "function signature" so
to say, for the uniform parameters. You feed these parameters 
when you bind descriptor sets to the command buffer which you
are currently recording. A pipeline bound to the same command
buffer will then use these inputs.

Note that you *don't* bind to the pipeline directly, 
but you bind both pipeline layout and descriptor sets 
TO THE CURRENT COMMAND BUFFER. 

Imagine the Command Buffer as the plugboard, and the Pipeline 
Layout plugging wires in on one side, and the descriptor sets 
plugging wires in on the other side. 

A pipeline can have some dynamic state, that is state which is
controlled by the command buffer. State which may be dynamic is
pretty limited, and has to be defined when you create a pipeline.

When a pipeline is created it is effectively compiled into a 
GPU program. Different non-dynamic pipeline state needs a different
pipeline. That's why you potentially need a pipeline for all 
possible combinations of states that you may use.

## Mission Statement

This class helps you create pipelines, and it also wraps pipeline
caching, so that pipelines can be requested based on dynamic state
and will be either created dynamically or created upfront. 

This class shall also help you to create pipeline layouts, based
on how your shaders are defined. It will try to match shader 
information gained through reflection (using spriv-cross) with 
descriptorSetLayouts to see if things are compatible.

The API will return VK handles, so that other libraries can be used
on top or alternatively to this one.


*/

namespace of{
namespace vk{

class Shader;

class GraphicsPipelineState
{

	// The idea is to have the context hold a pipeline in memory, 
	// and with each draw command store the current pipeline's 
	// hash into the command batch. 

	// when we build the command buffer, we need to check 
	// if the current context state is matched by an already 
	// available pipeline. 
	// 
	// if it isn't, we have to compile a pipeline for the command
	// 
	// if it is, we bind that pipeline.


public:	// default state for pipeline

	::vk::PipelineInputAssemblyStateCreateInfo mInputAssemblyState;
	::vk::PipelineTessellationStateCreateInfo  mTessellationState;
	::vk::PipelineViewportStateCreateInfo      mViewportState;
	::vk::PipelineRasterizationStateCreateInfo mRasterizationState;
	::vk::PipelineMultisampleStateCreateInfo   mMultisampleState;
	::vk::PipelineDepthStencilStateCreateInfo  mDepthStencilState;
	
	std::vector<::vk::PipelineColorBlendAttachmentState>    mBlendAttachmentStates;
	::vk::PipelineColorBlendStateCreateInfo    mColorBlendState;
	
	std::array<::vk::DynamicState, 2>          mDynamicStates;
	::vk::PipelineDynamicStateCreateInfo       mDynamicState;

private:

	::vk::RenderPass  mRenderPass         = nullptr;
	uint32_t          mSubpass            = 0;
	int32_t           mBasePipelineIndex  = -1;

	// shader allows us to derive pipeline layout
	std::shared_ptr<of::vk::Shader>        mShader;

public:

	void setup();
	void reset();

	uint64_t calculateHash();

	// whether this pipeline state is dirty.
	VkBool32          mDirty              = true;

	void setShader(const std::shared_ptr<of::vk::Shader> & shader ){
		if ( shader.get() != mShader.get() ){
			mShader = shader;
			mDirty = true;
		}
	}

	const std::shared_ptr<of::vk::Shader> getShader() const{
		return mShader;
	}

	void setRenderPass( const ::vk::RenderPass& renderPass ){
		if ( renderPass != mRenderPass ){
			mRenderPass = renderPass;
			mDirty = true;
		}
	}

	void setPolyMode( const ::vk::PolygonMode & polyMode){
		if ( mRasterizationState.polygonMode != polyMode ){
			mRasterizationState.polygonMode = polyMode;
			mDirty = true;
		}
	}

	::vk::Pipeline createPipeline( const ::vk::Device& device, const ::vk::PipelineCache& pipelineCache, ::vk::Pipeline basePipelineHandle = nullptr );

};

// ----------------------------------------------------------------------

/// \brief  Create a pipeline cache object
/// \detail Optionally load from disk, if filepath given.
/// \note  	Ownership: passed on.
static ::vk::PipelineCache&& createPipelineCache( const ::vk::Device& device, std::string filePath = "" ){
	::vk::PipelineCache cache;
	ofBuffer cacheFileBuffer;

	::vk::PipelineCacheCreateInfo info;

	if ( ofFile( filePath ).exists() ){
		cacheFileBuffer = ofBufferFromFile( filePath, true );
		info.setInitialDataSize(cacheFileBuffer.size());
		info.setPInitialData(cacheFileBuffer.getData());
	}

	cache = device.createPipelineCache( info );

	return std::move( cache );
};

} // namespace vk
} // namespace of
