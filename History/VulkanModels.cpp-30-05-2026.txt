// ============================================================================
// VulkanModels.cpp — Vulkan model resource implementation
//
// Implements VulkanModelUtils helpers and VulkanModelBuffers methods.
// ============================================================================

#include "Includes.h"

#if defined(__USE_VULKAN__)

#include "VulkanModels.h"
#include "Models.h"
#include "Debug.h"

extern Debug debug;

// ---------------------------------------------------------------------------
// stb_image — only define once across the project.
// If OpenGLModels.cpp is also compiled, use a different translation unit.
// Guard with a unique define so it's included exactly once.
// ---------------------------------------------------------------------------
#if !defined(STB_IMAGE_IMPLEMENTATION)
    #define STB_IMAGE_IMPLEMENTATION
#endif
#pragma warning(push)
#pragma warning(disable: 4244 4267 4996)
#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#elif __has_include("stb_image.h")
    #include "stb_image.h"
#else
    #define STBI_NOT_AVAILABLE
#endif
#pragma warning(pop)

#include <fstream>

// ============================================================================
// VulkanModelUtils implementation
// ============================================================================
namespace VulkanModelUtils
{
    uint32_t FindMemoryType(VkPhysicalDevice physDevice,
                            uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        return UINT32_MAX;
    }

    bool CreateBuffer(VkDevice device, VkPhysicalDevice physDevice,
                      VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& outBuffer, VkDeviceMemory& outMemory)
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = size;
        bufInfo.usage       = usage;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufInfo, nullptr, &outBuffer) != VK_SUCCESS)
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] vkCreateBuffer failed.");
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDevice,
                                                    memReq.memoryTypeBits, properties);
        if (allocInfo.memoryTypeIndex == UINT32_MAX)
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] No suitable memory type found for buffer.");
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] vkAllocateMemory failed.");
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(device, outBuffer, outMemory, 0);
        return true;
    }

    void CopyBuffer(VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                    VkBuffer src, VkBuffer dst, VkDeviceSize size,
                    std::mutex* queueMutex)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = cmdPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd{};
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        // Serialise against the render thread's vkQueueSubmit in RenderFrame().
        // Both threads share the same VkQueue handle; Vulkan requires external
        // synchronisation, so we must hold the same mutex the render thread uses.
        if (queueMutex) {
            std::lock_guard<std::mutex> qlock(*queueMutex);
            vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        } else {
            vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        }

        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    }

    bool UploadVertexBuffer(VkDevice device, VkPhysicalDevice physDevice,
                            VkCommandPool cmdPool, VkQueue queue,
                            const void* data, VkDeviceSize size,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                            std::mutex* queueMutex)
    {
        VkBuffer stagingBuf; VkDeviceMemory stagingMem;
        if (!CreateBuffer(device, physDevice, size,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuf, stagingMem))
            return false;

        void* mapped = nullptr;
        vkMapMemory(device, stagingMem, 0, size, 0, &mapped);
        memcpy(mapped, data, (size_t)size);
        vkUnmapMemory(device, stagingMem);

        bool ok = CreateBuffer(device, physDevice, size,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               outBuffer, outMemory);
        if (ok)
            CopyBuffer(device, cmdPool, queue, stagingBuf, outBuffer, size, queueMutex);

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
        return ok;
    }

    bool UploadIndexBuffer(VkDevice device, VkPhysicalDevice physDevice,
                           VkCommandPool cmdPool, VkQueue queue,
                           const uint32_t* indices, size_t indexCount,
                           VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                           std::mutex* queueMutex)
    {
        VkDeviceSize size = indexCount * sizeof(uint32_t);
        VkBuffer stagingBuf; VkDeviceMemory stagingMem;
        if (!CreateBuffer(device, physDevice, size,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuf, stagingMem))
            return false;

        void* mapped = nullptr;
        vkMapMemory(device, stagingMem, 0, size, 0, &mapped);
        memcpy(mapped, indices, (size_t)size);
        vkUnmapMemory(device, stagingMem);

        bool ok = CreateBuffer(device, physDevice, size,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               outBuffer, outMemory);
        if (ok)
            CopyBuffer(device, cmdPool, queue, stagingBuf, outBuffer, size, queueMutex);

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
        return ok;
    }

    void DestroyTexture(VkDevice device,
                        VkImage& image, VkDeviceMemory& memory, VkImageView& view)
    {
        if (view   != VK_NULL_HANDLE) { vkDestroyImageView(device, view, nullptr);   view   = VK_NULL_HANDLE; }
        if (image  != VK_NULL_HANDLE) { vkDestroyImage(device, image, nullptr);      image  = VK_NULL_HANDLE; }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(device, memory, nullptr);       memory = VK_NULL_HANDLE; }
    }

    std::vector<uint32_t> CompileGLSLToSPIRV(const std::wstring& glslFilePath,
                                               VkShaderStageFlagBits stage)
    {
#if __has_include(<shaderc/shaderc.hpp>)
        std::string narrow(glslFilePath.begin(), glslFilePath.end());
        std::ifstream file(narrow);
        if (!file.is_open())
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] Cannot open GLSL file: %ls", glslFilePath.c_str());
            return {};
        }
        std::string src((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

        shaderc_shader_kind kind = shaderc_glsl_vertex_shader;
        if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)  kind = shaderc_glsl_fragment_shader;
        else if (stage == VK_SHADER_STAGE_GEOMETRY_BIT) kind = shaderc_glsl_geometry_shader;
        else if (stage == VK_SHADER_STAGE_COMPUTE_BIT)  kind = shaderc_glsl_compute_shader;

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        auto result = compiler.CompileGlslToSpv(src, kind, narrow.c_str(), options);
        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] shaderc error: %hs", result.GetErrorMessage().c_str());
            return {};
        }
        return { result.cbegin(), result.cend() };
#else
        // shaderc not available — try to load a pre-compiled .spv alongside the .vert/.frag
        std::wstring spvPath = glslFilePath + L".spv";
        std::string narrow(spvPath.begin(), spvPath.end());
        std::ifstream file(narrow, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[VulkanModels] No shaderc and no .spv found for: %ls", glslFilePath.c_str());
            return {};
        }
        size_t sz = (size_t)file.tellg();
        file.seekg(0);
        std::vector<uint32_t> spv(sz / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(spv.data()), (std::streamsize)sz);
        return spv;
#endif
    }

    VkImageView LoadTextureFromFile(VkDevice device, VkPhysicalDevice physDevice,
                                    VkCommandPool cmdPool, VkQueue queue,
                                    const std::wstring& path,
                                    VkImage& outImage, VkDeviceMemory& outMemory)
    {
#if defined(STBI_NOT_AVAILABLE)
        (void)device; (void)physDevice; (void)cmdPool; (void)queue;
        (void)path; (void)outImage; (void)outMemory;
        return VK_NULL_HANDLE;
#else
        std::string narrow(path.begin(), path.end());
        std::ifstream f(narrow, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return VK_NULL_HANDLE;
        size_t sz = (size_t)f.tellg(); f.seekg(0);
        std::vector<uint8_t> buf(sz);
        f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)sz);
        return LoadTextureFromMemory(device, physDevice, cmdPool, queue,
                                     buf.data(), buf.size(), outImage, outMemory);
#endif
    }

    // -----------------------------------------------------------------------
    // Internal helper: transition an image layout via a single-time command buffer.
    // -----------------------------------------------------------------------
    static void TransitionImageLayout(VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                      VkImage image,
                                      VkImageLayout oldLayout, VkImageLayout newLayout,
                                      std::mutex* queueMutex = nullptr)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = cmdPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = oldLayout;
        barrier.newLayout           = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        if (queueMutex) {
            std::lock_guard<std::mutex> qlock(*queueMutex);
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        } else {
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        }
        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    }

    // -----------------------------------------------------------------------
    // Internal helper: copy a buffer into a 2D VkImage via a single-time CB.
    // -----------------------------------------------------------------------
    static void CopyBufferToImage(VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   VkBuffer buffer, VkImage image,
                                   uint32_t width, uint32_t height,
                                   std::mutex* queueMutex = nullptr)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = cmdPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        if (queueMutex) {
            std::lock_guard<std::mutex> qlock(*queueMutex);
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        } else {
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);
        }
        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    }

    // -----------------------------------------------------------------------
    // Internal helper: upload raw RGBA pixels to a new VkImage.
    // -----------------------------------------------------------------------
    static VkImageView UploadPixelsToImage(VkDevice device, VkPhysicalDevice physDevice,
                                            VkCommandPool cmdPool, VkQueue queue,
                                            const uint8_t* pixels, uint32_t w, uint32_t h,
                                            VkImage& outImage, VkDeviceMemory& outMemory,
                                            std::mutex* queueMutex = nullptr)
    {
        VkDeviceSize imgSize = static_cast<VkDeviceSize>(w) * h * 4;

        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateBuffer(device, physDevice, imgSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuf, stagingMem))
            return VK_NULL_HANDLE;

        void* mapped = nullptr;
        vkMapMemory(device, stagingMem, 0, imgSize, 0, &mapped);
        std::memcpy(mapped, pixels, static_cast<size_t>(imgSize));
        vkUnmapMemory(device, stagingMem);

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.extent        = { w, h, 1 };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.format        = VK_FORMAT_R8G8B8A8_SRGB;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device, &imgInfo, nullptr, &outImage) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return VK_NULL_HANDLE;
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, outImage, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDevice, memReq.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocInfo.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
        {
            vkDestroyImage(device, outImage, nullptr);
            outImage = VK_NULL_HANDLE;
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return VK_NULL_HANDLE;
        }
        vkBindImageMemory(device, outImage, outMemory, 0);

        TransitionImageLayout(device, cmdPool, queue, outImage,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, queueMutex);
        CopyBufferToImage(device, cmdPool, queue, stagingBuf, outImage, w, h, queueMutex);
        TransitionImageLayout(device, cmdPool, queue, outImage,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, queueMutex);

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = outImage;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
        {
            vkDestroyImage(device, outImage, nullptr); outImage = VK_NULL_HANDLE;
            vkFreeMemory(device, outMemory, nullptr);  outMemory = VK_NULL_HANDLE;
        }
        return view;
    }

    VkImageView LoadTextureFromMemory(VkDevice device, VkPhysicalDevice physDevice,
                                      VkCommandPool cmdPool, VkQueue queue,
                                      const uint8_t* data, size_t size,
                                      VkImage& outImage, VkDeviceMemory& outMemory)
    {
#if defined(STBI_NOT_AVAILABLE)
        (void)device; (void)physDevice; (void)cmdPool; (void)queue;
        (void)data; (void)size; (void)outImage; (void)outMemory;
        return VK_NULL_HANDLE;
#else
        int w = 0, h = 0, ch = 0;
        uint8_t* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &ch, STBI_rgb_alpha);
        if (!pixels)
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[VulkanModels] stbi_load_from_memory failed.");
            return VK_NULL_HANDLE;
        }

        VkImageView view = UploadPixelsToImage(device, physDevice, cmdPool, queue,
                                               pixels, (uint32_t)w, (uint32_t)h,
                                               outImage, outMemory);
        stbi_image_free(pixels);
        return view;
#endif
    }

    VkImageView CreateSolidColourTexture(VkDevice device, VkPhysicalDevice physDevice,
                                          VkCommandPool cmdPool, VkQueue queue,
                                          const Vector4& colour,
                                          VkImage& outImage, VkDeviceMemory& outMemory)
    {
        // Build a 1×1 RGBA pixel directly — do NOT pass these 4 bytes through stbi
        // because stbi expects image file data (PNG/JPEG header), not raw RGBA.
        uint8_t rgba[4] = {
            static_cast<uint8_t>(std::clamp(colour.x, 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(std::clamp(colour.y, 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(std::clamp(colour.z, 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(std::clamp(colour.w, 0.0f, 1.0f) * 255.0f)
        };
        return UploadPixelsToImage(device, physDevice, cmdPool, queue,
                                   rgba, 1, 1, outImage, outMemory);
    }
}

// ============================================================================
// VulkanModelBuffers implementation
// ============================================================================
bool VulkanModelBuffers::Upload(VkDevice device, VkPhysicalDevice physDevice,
                                 VkCommandPool cmdPool, VkQueue queue,
                                 const void* vertData, size_t vertBytes,
                                 const uint32_t* indexData, size_t idxCount,
                                 std::mutex* queueMutex)
{
    indexCount = (uint32_t)idxCount;

    bool ok = VulkanModelUtils::UploadVertexBuffer(device, physDevice, cmdPool, queue,
                                                    vertData, (VkDeviceSize)vertBytes,
                                                    vertexBuffer, vertexBufferMemory,
                                                    queueMutex);
    if (!ok) return false;

    ok = VulkanModelUtils::UploadIndexBuffer(device, physDevice, cmdPool, queue,
                                              indexData, idxCount,
                                              indexBuffer, indexBufferMemory,
                                              queueMutex);
    return ok;
}

void VulkanModelBuffers::Destroy(VkDevice device)
{
    if (indexBuffer  != VK_NULL_HANDLE) { vkDestroyBuffer(device, indexBuffer, nullptr);  indexBuffer  = VK_NULL_HANDLE; }
    if (indexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, indexBufferMemory, nullptr); indexBufferMemory = VK_NULL_HANDLE; }
    if (vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, vertexBuffer, nullptr); vertexBuffer = VK_NULL_HANDLE; }
    if (vertexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, vertexBufferMemory, nullptr); vertexBufferMemory = VK_NULL_HANDLE; }

    VulkanModelUtils::DestroyTexture(device, diffuseImage, diffuseMemory, diffuseView);
    VulkanModelUtils::DestroyTexture(device, normalImage,  normalMemory,  normalView);
    VulkanModelUtils::DestroyTexture(device, metallicImage,metallicMemory,metallicView);
    VulkanModelUtils::DestroyTexture(device, roughImage,   roughMemory,   roughView);
    VulkanModelUtils::DestroyTexture(device, aoImage,      aoMemory,      aoView);

    if (sampler != VK_NULL_HANDLE) { vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE; }

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        if (uniformBufferMemory[i] != VK_NULL_HANDLE) vkFreeMemory(device, uniformBufferMemory[i], nullptr);
    }
    uniformBuffers.clear();
    uniformBufferMemory.clear();
}

#endif // __USE_VULKAN__
