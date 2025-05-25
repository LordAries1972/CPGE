#define NOMINMAX
#include "Includes.h"
#include "Configuration.h"
#include "Models.h"
#include "ConstantBuffer.h"
#include "DX_FXManager.h"
#include "DXCamera.h" // For matrix usage if needed
#include "Debug.h"
#include "WinSystem.h"
#include "ThreadManager.h"
#include "RendererMacros.h"

#ifdef __USE_DIRECTX_11__
#include <d3d11.h>
#include <wincodec.h>
#include <d3dcompiler.h> // For D3DCompileFromFile

// Linking DirectX 11 libraries
#pragma comment(lib, "d3dcompiler.lib") // Link D3DCompiler library
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

// Requires valid D3D11 device (assume global/context access)
#endif

extern std::shared_ptr<Renderer> renderer;
extern Model models[MAX_MODELS];                                         // Our Models Data, defined in main.cpp
extern Debug debug;
extern FXManager fxManager;
extern SystemUtils sysUtils;
extern ThreadManager threadManager;
extern Configuration config;

//==============================================================================
// Texture Implementation
//==============================================================================
Texture::Texture() = default;

Texture::Texture(const std::wstring& path) {
    LoadFromFile(path);
}

Texture::~Texture() {
    if (bTextureDestroyed) return;
#ifdef __USE_DIRECTX_11__
    if (textureSRV) textureSRV->Release();
    if (textureResource) textureResource->Release();
#endif

    bTextureDestroyed = true;
}

bool Texture::LoadFromFile(const std::wstring& path) {
    texturePath = path;

#ifdef __USE_DIRECTX_11__
    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
    ComPtr<ID3D11Device> device = dx11->m_d3dDevice;
    if (!device) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: No device context available");
        return false;
    }

    // Reset previous SRV if reused
    if (textureSRV) {
        textureSRV->Release();
        textureSRV = nullptr;
    }

    if (!std::filesystem::exists(path)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Texture file does not exist: " + path);
        return false;
    }

    IWICImagingFactory* wicFactory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return false;

    hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: Failed to decode texture: " + path);
        wicFactory->Release();
        return false;
    }

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        decoder->Release(); wicFactory->Release();
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: No frame found in texture: " + path);
        return false;
    }

    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        frame->Release(); decoder->Release(); wicFactory->Release();
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: Format converter creation failed for: " + path);
        return false;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: Converter initialization failed for: " + path);
        return false;
    }

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    std::vector<BYTE> pixels(width * height * 4);
    converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* tex = nullptr;
    hr = device->CreateTexture2D(&desc, &initData, &tex);
    if (FAILED(hr)) {
        converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: Failed to create texture from pixel data: " + path);
        return false;
    }

    hr = device->CreateShaderResourceView(tex, nullptr, &textureSRV);
    tex->Release();
    converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX11: SRV creation failed for: " + path);
        return false;
    }

    #if defined(_DEBUG_MODEL_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DX11 Texture loaded: " + path);
    #endif
    return true;
#else
    return false;
#endif
}

// ==========================================================================================
// CreateSolidColorTexture
// Creates a 2D texture filled with a constant color.
// ==========================================================================================
bool Texture::CreateSolidColorTexture(uint32_t width, uint32_t height, const XMFLOAT4& color)
{
    if (width == 0 || height == 0)
        return false;

    // Create CPU-side color buffer
    std::vector<uint8_t> textureData(width * height * 4); // 4 bytes per pixel (RGBA)

    uint8_t r = static_cast<uint8_t>(color.x * 255.0f);
    uint8_t g = static_cast<uint8_t>(color.y * 255.0f);
    uint8_t b = static_cast<uint8_t>(color.z * 255.0f);
    uint8_t a = static_cast<uint8_t>(color.w * 255.0f);

    for (uint32_t i = 0; i < width * height; ++i)
    {
        textureData[i * 4 + 0] = r; // Red
        textureData[i * 4 + 1] = g; // Green
        textureData[i * 4 + 2] = b; // Blue
        textureData[i * 4 + 3] = a; // Alpha
    }

    std::shared_ptr<DX11Renderer> dx11;
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> renderer) {
        dx11 = renderer;
        });

    // Describe texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = textureData.data();
    initData.SysMemPitch = width * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2D;

    HRESULT hr = dx11->m_d3dDevice->CreateTexture2D(&texDesc, &initData, &texture2D);
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[Texture] Failed to create solid color texture. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = dx11->m_d3dDevice->CreateShaderResourceView(texture2D.Get(), &srvDesc, &textureSRV);
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[Texture] Failed to create SRV for solid color texture. HRESULT: 0x%08X", hr);
        return false;
    }

    return true;
}

//==============================================================================
// Model Class Implementation
//==============================================================================

// Constructor.
Model::Model() : m_isLoaded(false), m_animationTime(0.0f) {
    SecureZeroMemory(&m_modelInfo, sizeof(m_modelInfo));
	m_modelInfo.name = L"";

    // Explicitly initialize vectors to avoid uninitialized memory issues
    m_modelInfo.vertices = std::vector<Vertex>();
    m_modelInfo.indices = std::vector<uint32_t>();
    m_modelInfo.tempPositions = std::vector<XMFLOAT3>();
    m_modelInfo.tempNormals = std::vector<XMFLOAT3>();
    m_modelInfo.tempTexCoords = std::vector<XMFLOAT2>();
}

// Destructor.
Model::~Model() {
    if (bIsDestroyed) return;
    DestroyModel();
    bIsDestroyed = true;
}

// Loads a model from file. Supports both ".obj" and ".blend" formats.
bool Model::LoadModel(const std::wstring& filename, int ID) {
    // Thread Safety Lock for Render and Loader Threads.
//    std::lock_guard<std::mutex> lock(m_ModelMutex);
    // Initialize model info
    m_modelInfo.ID = ID;
    m_modelInfo.vertices.clear();
    m_modelInfo.indices.clear();
    m_modelInfo.textures.clear();
    m_modelInfo.materials.clear();

    bool result = false;
    std::filesystem::path filePath(filename);
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == ".obj") {
        std::string narrowPath(filename.begin(), filename.end());
        result = LoadOBJ(narrowPath);
    }
    else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Unsupported file format: " + std::wstring(extension.begin(), extension.end()));
        return false;
    }

    if (result) {
        m_isLoaded = true;
        #if defined(_DEBUG_MODEL_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Model loaded successfully.");
        #endif
    }

    return result;
}

// Updates the model’s animation state.
void Model::UpdateAnimation(float deltaTime)
{
#if defined(_DEBUG_MODEL_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Model ID %d world matrix updated at t=%.2f", m_modelInfo.ID, deltaTime);
#endif

    // If animation vertices exist, apply transform updates
    if (!m_modelInfo.animationVertices.empty())
    {
        m_animationTime += deltaTime;

        float angle = m_animationTime;
        XMMATRIX scale = XMMatrixScaling(m_modelInfo.scale.x, m_modelInfo.scale.y, m_modelInfo.scale.z);
        XMMATRIX rotate = XMMatrixRotationRollPitchYaw(m_modelInfo.rotation.x, m_modelInfo.rotation.y + angle, m_modelInfo.rotation.z);
        XMMATRIX translate = XMMatrixTranslation(m_modelInfo.position.x, m_modelInfo.position.y, m_modelInfo.position.z);

        m_modelInfo.worldMatrix = scale * rotate * translate;

#if defined(_DEBUG_MODEL_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ANIM] World matrix overridden via animation logic.");
#endif
    }
    else
    {
        // 🔒 Do not override worldMatrix if it's from GLTF transform
#if defined(_DEBUG_MODEL_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ANIM] No animation: Preserving GLTF world matrix.");
#endif
    }
}

// Models.cpp
void Model::DestroyModel()
{
    if ((!m_isLoaded) || (bIsDestroyed)) return;

#if defined(_DEBUG_MODEL_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Model] DestroyModel() called for model name: %hs", m_modelInfo.modelName.c_str());
#endif

    // =========================================================
    // Release All DirectX GPU Resources
    // =========================================================
    if (m_modelInfo.vertexBuffer) { m_modelInfo.vertexBuffer->Release(); }
    if (m_modelInfo.indexBuffer) { m_modelInfo.indexBuffer->Release(); }
    if (m_modelInfo.materialBuffer) { m_modelInfo.materialBuffer->Release(); }
    if (m_modelInfo.lightConstantBuffer) { m_modelInfo.lightConstantBuffer->Release(); }
    if (m_modelInfo.debugConstantBuffer) { m_modelInfo.debugConstantBuffer->Release(); }

    if (threadManager.threadVars.bIsShuttingDown.load())
    {
        if (m_modelInfo.samplerState) { m_modelInfo.samplerState->Release(); }
        if (m_modelInfo.environmentSamplerState) { m_modelInfo.environmentSamplerState->Release(); }
        if (m_modelInfo.vertexShader) { m_modelInfo.vertexShader->Release(); }
		if (m_modelInfo.pixelShader) { m_modelInfo.pixelShader->Release(); }
		if (m_modelInfo.inputLayout) { m_modelInfo.inputLayout->Release(); }
		if (m_modelInfo.vertexShaderBlob) { m_modelInfo.vertexShaderBlob->Release(); }
		if (m_modelInfo.pixelShaderBlob) { m_modelInfo.pixelShaderBlob->Release(); }
		if (m_modelInfo.constantBuffer) { m_modelInfo.constantBuffer->Release(); }
    }

    // =========================================================
    // Release and Clear All Textures and Shader Resource Views
    // =========================================================
    for (auto& srv : m_modelInfo.textures)
    {
		srv.reset();
    }
    m_modelInfo.textures.clear();
	m_modelInfo.textures.shrink_to_fit();

    for (auto& srv : m_modelInfo.textureSRVs)
    {
        if (srv) srv.Reset();
    }
    m_modelInfo.textureSRVs.clear();
    m_modelInfo.textureSRVs.shrink_to_fit();

    for (auto& srv : m_modelInfo.normalMapSRVs)
    {
        if (srv) srv.Reset();
    }
    m_modelInfo.normalMapSRVs.clear();
	m_modelInfo.normalMapSRVs.shrink_to_fit();

    // =========================================================
    // Clear All Materials
    // =========================================================
    m_materials.clear();

    // =========================================================
    // Clear All Geometry Data
    // =========================================================
    m_modelInfo.vertices.clear();
    m_modelInfo.indices.clear();
    m_modelInfo.tempPositions.clear();
	m_modelInfo.tempNormals.clear();
	m_modelInfo.tempTexCoords.clear();
    m_modelInfo.animationVertices.clear(); // If used for animations

    // =========================================================
    // Reset Primitive Counts and Flags
    // =========================================================
    m_modelInfo.iAnimationIndex = 0;
    m_animationTime = 0.0f;

    // =========================================================
    // Reset Transformation Data
    // =========================================================
    m_modelInfo.position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_modelInfo.scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
    m_modelInfo.rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_modelInfo.worldMatrix = DirectX::XMMatrixIdentity();

    // =========================================================
    // Reset Lighting Information
    // =========================================================
    m_modelInfo.localLights.clear();
	m_modelInfo.localLights.shrink_to_fit();

    m_modelInfo.fxActive = false;
    if (threadManager.threadVars.bIsResizing.load())
	{
        ModelInfo newModelInfo;
        m_modelInfo = newModelInfo;                                     // Reset model info to default state
		SecureZeroMemory(&m_modelInfo, sizeof(m_modelInfo));            // Clear the model info struct to avoid double destruction.
        m_modelInfo.name = L"";

        // Explicitly initialize vectors to avoid uninitialized memory issues
        m_modelInfo.vertices = std::vector<Vertex>();
        m_modelInfo.indices = std::vector<uint32_t>();
        m_modelInfo.tempPositions = std::vector<XMFLOAT3>();
        m_modelInfo.tempNormals = std::vector<XMFLOAT3>();
        m_modelInfo.tempTexCoords = std::vector<XMFLOAT2>();
    }
    else
    {
        bIsDestroyed = true;
    }

    // =========================================================
	// Reset Loaded State
	// =========================================================
	m_isLoaded = false;
    if (threadManager.threadVars.bIsShuttingDown.load())
    {
		SecureZeroMemory(&m_modelInfo, sizeof(m_modelInfo));
		m_modelInfo.name = L"";
		m_modelInfo.ID = -1;
		m_modelInfo.fxID = -1;
		m_modelInfo.iAnimationIndex = -1;
		m_modelInfo.fxActive = false;
	}

#if defined(_DEBUG_MODEL_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Model] DestroyModel() completed for model name: %hs", m_modelInfo.modelName.c_str());
#endif
}

void Model::ApplyDefaultLightingFromManager(LightsManager& myLightsManager)
{
#if defined(_DEBUG_MODEL_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Applying global lights from LightsManager to model ID %d", m_modelInfo.ID);
#endif

    std::vector<LightStruct> globalLights = myLightsManager.GetAllLights();

    // Clamp to max shader lights if needed
    if (globalLights.size() > MAX_MODEL_LIGHTS)
        globalLights.resize(MAX_MODEL_LIGHTS);

    m_modelInfo.localLights = globalLights;

}

bool Model::LoadMTL(const std::wstring& mtlPath)
{
    std::wstring fileName = sysUtils.StripQuotes(mtlPath);
    std::ifstream file(fileName);
    if (!file.is_open())
    {
#if defined(_DEBUG_MODEL_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Model: Failed to open MTL file \"" + fileName + L"\"");
#endif
        return false;
    }

    std::string line;
    Material currentMat;

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "newmtl")
        {
            if (!currentMat.name.empty())
            {
                m_materials[currentMat.name] = currentMat;
            }

            iss >> currentMat.name;
            currentMat.diffuseMapPath.clear();
            currentMat.diffuseTexture = nullptr;
            currentMat.Kd = { 1.0f, 1.0f, 1.0f };
            currentMat.Ka = { 0.1f, 0.1f, 0.1f };
            currentMat.Ks = { 0.5f, 0.5f, 0.5f };
            currentMat.Ns = 32.0f;
        }
        else if (tag == "map_Kd")
        {
            std::string texPath;
            iss >> texPath;

            currentMat.diffuseMapPath = texPath;

            auto tex = std::make_shared<Texture>();
            std::wstring txPath = sysUtils.ToWString(texPath);
            std::wstring texPathStr = sysUtils.StripQuotes(txPath);
            std::wstring myFilename = AssetsDir / texPathStr;

#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"LoadMTL(): Model: Attempting to Load texture from " + myFilename);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"LoadMTL(): → Attempting to load image URI: %hs", texPathStr.c_str());
            debug.logDebugMessage(LogLevel::LOG_INFO, L"LoadMTL(): → Resolved full texture path: %ls", myFilename.c_str());
#endif
            if (tex->LoadFromFile(myFilename))
            {
                currentMat.diffuseTexture = tex;
#if defined(_DEBUG_MODEL_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Model: Loaded texture for material " + std::wstring(currentMat.name.begin(), currentMat.name.end()));
#endif
            }
            else
            {
#if defined(_DEBUG_MODEL_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model: Failed to load texture: " + std::wstring(texPath.begin(), texPath.end()));
#endif
            }
        }
        else if (tag == "map_Bump" || tag == "bump")
        {
            std::string bumpPath;
            iss >> bumpPath;
            currentMat.normalMapPath = bumpPath;

            auto bumpTex = std::make_shared<Texture>();
            std::wstring newMtlFile = sysUtils.ToWString(bumpPath);
            std::wstring fileName = sysUtils.StripQuotes(newMtlFile);
            std::filesystem::path fullPath = AssetsDir / fileName;
#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"LoadMTL(): Model: Attempting to Load Materia texture from " + fileName);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"LoadMTL(): → Attempting to load Material image URI: %hs", fileName.c_str());
            debug.logDebugMessage(LogLevel::LOG_INFO, L"LoadMTL(): → Resolved full Material path: %ls", fullPath.c_str());
#endif
            if (bumpTex->LoadFromFile(fullPath))
                currentMat.normalMap = bumpTex;
            else
                currentMat.normalMap = nullptr;
        }
        else if (tag == "map_Ka") {
            std::string texPath;
            iss >> texPath;
            std::wstring newMtlFile = sysUtils.ToWString(texPath);
            std::wstring fileName = sysUtils.StripQuotes(newMtlFile);
            std::filesystem::path fullPath = AssetsDir / fileName;

            auto tex = std::make_shared<Texture>();
            if (tex->LoadFromFile(fullPath)) {
                currentMat.ambientTexture = tex;
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model: Failed to load ambient texture: " + sysUtils.ToWString(texPath));
            }
        }
        else if (tag == "map_Ks") {
            std::string texPath;
            iss >> texPath;
            std::wstring newMtlFile = sysUtils.ToWString(texPath);
            std::wstring fileName = sysUtils.StripQuotes(newMtlFile);
            std::filesystem::path fullPath = AssetsDir / fileName;
            currentMat.specularMapPath = fullPath;

            auto tex = std::make_shared<Texture>();
            if (tex->LoadFromFile(fullPath)) {
                currentMat.specularTexture = tex;
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model: Failed to load specular texture: " + sysUtils.ToWString(texPath));
            }
        }
        else if (tag == "d") {
            iss >> currentMat.dissolve;
        }
        else if (tag == "illum") {
            iss >> currentMat.illumModel;
        }
        else if (tag == "Kd")
        {
            iss >> currentMat.Kd.x >> currentMat.Kd.y >> currentMat.Kd.z;
        }
        else if (tag == "Ka")
        {
            iss >> currentMat.Ka.x >> currentMat.Ka.y >> currentMat.Ka.z;
        }
        else if (tag == "Ks")
        {
            iss >> currentMat.Ks.x >> currentMat.Ks.y >> currentMat.Ks.z;
        }
        else if (tag == "Ns")
        {
            iss >> currentMat.Ns;
        }
    }

    // Don't forget the final material
    if (!currentMat.name.empty())
    {
        m_materials[currentMat.name] = currentMat;
    }

    file.close();
    return true;
}

void Model::LoadFallbackTexture()
{
    static std::shared_ptr<Texture> fallback;

    if (!fallback)
    {
        auto fileName = AssetsDir / L"bricks1.png";
        if (!std::filesystem::exists(fileName)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Missing fallback texture: " + fileName.wstring());
            return;
        }

        fallback = std::make_shared<Texture>();
        fallback->LoadFromFile(fileName);           // provide a known good fallback
    }

    m_modelInfo.textures.push_back(fallback);
    m_modelInfo.textureSRVs = std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>();
    m_modelInfo.textureSRVs.push_back(fallback->GetSRV());
}

//==============================================================================
// Loads a model from a Wavefront OBJ file.
// In Models.cpp, add the following implementation:
//==============================================================================
bool Model::LoadOBJ(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::vector<XMFLOAT3> tempPositions;
    std::vector<XMFLOAT3> tempNormals;
    std::vector<XMFLOAT2> tempTexCoords;

    std::string currentMaterial;
    std::string mtlFileToLoad;
    std::unordered_map<std::string, UINT> uniqueVertices;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "mtllib")
        {
            iss >> mtlFileToLoad;
            std::wstring wfile = sysUtils.StripQuotes(sysUtils.ToWString(mtlFileToLoad));
            LoadMTL((AssetsDir / wfile).wstring());
        }
        else if (tag == "v")
        {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (tag == "vt")
        {
            XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            tempTexCoords.push_back(tex);
        }
        else if (tag == "vn")
        {
            XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        }
        else if (tag == "usemtl")
        {
            iss >> currentMaterial;
        }
        else if (tag == "f")
        {
            std::string v[3];
            // OBJ format can have their vertex indices in different orders
            if (config.myConfig.BackCulling)
            {
                // Go Forward direction.
                iss >> v[0] >> v[1] >> v[2];
            }
            else
            { 
                // Go Reverse direction.
                iss >> v[2] >> v[1] >> v[0];
            }
            
            for (int i = 0; i < 3; ++i)
            {
                std::istringstream vstream(v[i]);
                std::string posStr, texStr, normStr;
                std::getline(vstream, posStr, '/');
                std::getline(vstream, texStr, '/');
                std::getline(vstream, normStr, '/');

                int posIdx = std::stoi(posStr) - 1;
                int texIdx = texStr.empty() ? 0 : std::stoi(texStr) - 1;
                int normIdx = normStr.empty() ? 0 : std::stoi(normStr) - 1;

                Vertex vertex;
                vertex.position = tempPositions[posIdx];
                vertex.texCoord = texIdx >= 0 && texIdx < tempTexCoords.size() ? tempTexCoords[texIdx] : XMFLOAT2(0, 0);
                vertex.normal = normIdx >= 0 && normIdx < tempNormals.size() ? tempNormals[normIdx] : XMFLOAT3(0, 1, 0);
                vertex.tangent = XMFLOAT3(0, 0, 0); // will compute later

                m_modelInfo.vertices.push_back(vertex);
                m_modelInfo.indices.push_back(static_cast<uint32_t>(m_modelInfo.vertices.size() - 1));
            }

            if (std::find(m_modelInfo.materials.begin(), m_modelInfo.materials.end(), currentMaterial) == m_modelInfo.materials.end()) {
                m_modelInfo.materials.push_back(currentMaterial);
            }
        }
    }

    // Tangent calculation
    std::vector<XMFLOAT3> accumulatedTangents(m_modelInfo.vertices.size(), XMFLOAT3(0, 0, 0));
    std::vector<uint32_t> tangentCounts(m_modelInfo.vertices.size(), 0);

    for (size_t i = 0; i < m_modelInfo.indices.size(); i += 3)
    {
        uint32_t i0 = m_modelInfo.indices[i];
        uint32_t i1 = m_modelInfo.indices[i + 1];
        uint32_t i2 = m_modelInfo.indices[i + 2];

        Vertex& v0 = m_modelInfo.vertices[i0];
        Vertex& v1 = m_modelInfo.vertices[i1];
        Vertex& v2 = m_modelInfo.vertices[i2];

        XMVECTOR p0 = XMLoadFloat3(&v0.position);
        XMVECTOR p1 = XMLoadFloat3(&v1.position);
        XMVECTOR p2 = XMLoadFloat3(&v2.position);

        XMFLOAT2 uv0 = v0.texCoord;
        XMFLOAT2 uv1 = v1.texCoord;
        XMFLOAT2 uv2 = v2.texCoord;

        XMVECTOR deltaPos1 = p1 - p0;
        XMVECTOR deltaPos2 = p2 - p0;
        float du1 = uv1.x - uv0.x;
        float dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x;
        float dv2 = uv2.y - uv0.y;

//        float r = 1.0f / (du1 * dv2 - du2 * dv1);
        // Safe guard this and ensure no division by zero!
        float denom = (du1 * dv2 - du2 * dv1);
        float r = denom == 0.0f ? 1.0f : 1.0f / denom;

        XMVECTOR tangent = (deltaPos1 * dv2 - deltaPos2 * dv1) * r;

        for (uint32_t idx : { i0, i1, i2 }) {
            XMVECTOR oldTangent = XMLoadFloat3(&accumulatedTangents[idx]);
            oldTangent += tangent;
            XMStoreFloat3(&accumulatedTangents[idx], oldTangent);
            tangentCounts[idx]++;
        }
    }

    for (size_t i = 0; i < m_modelInfo.vertices.size(); ++i)
    {
        if (tangentCounts[i] > 0)
        {
            XMVECTOR t = XMLoadFloat3(&accumulatedTangents[i]);
            XMVECTOR n = XMLoadFloat3(&m_modelInfo.vertices[i].normal);

            // Gram-Schmidt orthogonalize tangent with normal
            t = XMVector3Normalize(t - n * XMVector3Dot(n, t));

            XMStoreFloat3(&m_modelInfo.vertices[i].tangent, t);
        }
        else
        {
            // Fallback to a default tangent if no tangents were calculated
            m_modelInfo.vertices[i].tangent = XMFLOAT3(1, 0, 0);
        }
    }

    return true;
}

//==============================================================================
// Helper function to compile shaders
//==============================================================================
HRESULT Model::CompileShaderFromFile(const std::wstring& filePath, const std::string& entryPoint, const std::string& shaderModel, ID3DBlob** blobOut) {
    HRESULT hr = S_OK;

    DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    shaderFlags |= D3DCOMPILE_DEBUG;
    shaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    if (!std::filesystem::exists(filePath)) {
#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Shader file NOT found!");
#endif
        return E_FAIL;
    }
    ID3DBlob* errorBlob = nullptr;
    hr = D3DCompileFromFile(
        filePath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        shaderModel.c_str(),
        shaderFlags,
        0,
        blobOut,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string errorMsg(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Shader compilation error: " + sysUtils.ToWString(errorMsg));
#endif
            errorBlob->Release();
        }
        return hr;
    }

    if (errorBlob) {
        errorBlob->Release();
    }

    return hr;
}

// --------------------------------------------------------------------------------------------------
// Model::CopyFrom
// Deeply copies all fields from another Model instance into this one.
// Ensures full structure integrity, no shared pointers, no shallow copies.
// --------------------------------------------------------------------------------------------------
void Model::CopyFrom(const Model& other)
{
    // === Basic Shallow Copy First ===
    m_modelInfo = other.m_modelInfo;

    if (threadManager.threadVars.bIsResizing)
    {
#if defined(_DEBUG_MODEL_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[Model::CopyFrom] Resize Detected → Resetting ONLY GPU resources (SRVs, Buffers, Shaders) - NOT textures!");
#endif
        // Clear SRVs
        for (auto& srv : m_modelInfo.textureSRVs)
            srv.Reset();
        m_modelInfo.textureSRVs.clear();
        m_modelInfo.textureSRVs.shrink_to_fit();

        for (auto& srv : m_modelInfo.normalMapSRVs)
            srv.Reset();
        m_modelInfo.normalMapSRVs.clear();
        m_modelInfo.normalMapSRVs.shrink_to_fit();

        m_modelInfo.metallicMapSRV.Reset();
        m_modelInfo.roughnessMapSRV.Reset();
        m_modelInfo.aoMapSRV.Reset();
        m_modelInfo.environmentMapSRV.Reset();

        // Clear GPU Buffers
        m_modelInfo.vertexBuffer.Reset();
        m_modelInfo.indexBuffer.Reset();
        m_modelInfo.constantBuffer.Reset();
        m_modelInfo.lightConstantBuffer.Reset();
        m_modelInfo.materialBuffer.Reset();
        m_modelInfo.environmentBuffer.Reset();

        // Clear Shaders
        m_modelInfo.vertexShader.Reset();
        m_modelInfo.pixelShader.Reset();
        m_modelInfo.vertexShaderBlob.Reset();
        m_modelInfo.pixelShaderBlob.Reset();

        // Clear Input Layout
        m_modelInfo.inputLayout.Reset();

        // Clear Sampler State
        m_modelInfo.samplerState.Reset();
        m_modelInfo.environmentSamplerState.Reset();

        // === 🛡️ DO NOT TOUCH: ===
        // m_modelInfo.textures
        // m_modelInfo.materials
        // m_materials
    }

    if (m_modelInfo.name.empty())
        m_modelInfo.name = L"UnnamedModel_" + std::to_wstring(m_modelInfo.ID);

    // Reset dynamic flags
    m_isLoaded = false;
    m_animationTime = 0.0f;
    bIsDestroyed = false;
}

// Updates the constant buffer with the world matrix.
void Model::UpdateConstantBuffer() {
#ifdef __USE_DIRECTX_11__
    // Ensure the model is loaded and the constant buffer is valid
    if (!m_isLoaded || !m_modelInfo.constantBuffer) {
#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateConstantBuffer: Model not loaded or constant buffer is invalid.");
#endif
        return;
    }

    // Get the device context from the renderer
    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
    ComPtr<ID3D11DeviceContext> deviceContext = dx11->m_d3dContext;
    ConstantBuffer cb = {};
    m_modelInfo.cameraPosition = dx11->myCamera.GetPosition();
    cb.worldMatrix = XMMatrixTranspose(m_modelInfo.worldMatrix);
    cb.viewMatrix = XMMatrixTranspose(m_modelInfo.viewMatrix);
    cb.projectionMatrix = XMMatrixTranspose(m_modelInfo.projectionMatrix);
    cb.cameraPosition = m_modelInfo.cameraPosition;
    cb.modelScale = m_modelInfo.scale;

    if (m_modelInfo.constantBuffer)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource = {};
        HRESULT hr = deviceContext->Map(m_modelInfo.constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));
            deviceContext->Unmap(m_modelInfo.constantBuffer.Get(), 0);
        }
    }
#else
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"UpdateConstantBuffer: DirectX 11 is not enabled.");
#endif
}

void Model::TriggerEffect(int effectID)
{
    m_modelInfo.fxID = effectID;
    m_modelInfo.fxActive = true;
}

void FXManager::CancelEffect(int effectID)
{
    effects.erase(std::remove_if(effects.begin(), effects.end(),
        [effectID](const FXItem& fx) { return fx.fxID == effectID; }),
        effects.end());
}

void FXManager::RestartEffect(int effectID)
{
    for (FXItem& fx : effects)
    {
        if (fx.fxID == effectID)
        {
            fx.startTime = std::chrono::high_resolution_clock::now();
            break;
        }
    }
}

void FXManager::ChainEffect(int fromEffectID, int toEffectID)
{
    for (FXItem& fx : effects)
    {
        if (fx.fxID == fromEffectID)
        {
            fx.nextEffectID = toEffectID;
            break;
        }
    }
}

void Model::SetPosition(XMFLOAT3 position)
{
    m_modelInfo.position = position;
}

void Model::LoadFallbackNormalMap()
{
    static std::shared_ptr<Texture> flatNormal;

    if (!flatNormal)
    {
        auto fileName = AssetsDir / L"flat_normal.png";  // Must exist!
        if (!std::filesystem::exists(fileName)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Missing fallback normal map: " + fileName.wstring());
            return;
        }

        flatNormal = std::make_shared<Texture>();
        flatNormal->LoadFromFile(fileName);
    }

    // SAFELY Reset the normalMapSRVs and textureSRVs before fallback loading
    m_modelInfo.normalMapSRVs = std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>();
    m_modelInfo.normalMapSRVs.push_back(flatNormal->GetSRV());
}

//==============================================================================
// Restored SetupModelForRendering with shader setup and DX11 macro protection
//==============================================================================
bool Model::SetupModelForRendering(int ID)
{ 
    bool result = false;
    result = SetupModelForRendering();

#if defined(_DEBUG_MODEL_) || defined(_DEBUG_SCENEMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Model] CopyFrom() completed for model name: \"%ls\"", m_modelInfo.name.c_str());
#endif

    return result;
}

bool Model::SetupModelForRendering()
{
#ifdef __USE_DIRECTX_11__
    std::shared_ptr<DX11Renderer> dx11;
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> renderer) {
        dx11 = renderer;

        if (!dx11 || !dx11->m_d3dDevice || !dx11->m_d3dContext) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID: %d Status: %d Failed to Setup (Device or Context could be NULL)", m_modelInfo.ID, m_isLoaded);
            return false;
        }

        ID3D11Device* device = dx11->m_d3dDevice.Get();
        ID3D11DeviceContext* context = dx11->m_d3dContext.Get();

        // If no SRVs were loaded, force a fallback so GPU doesn't crash
        if (m_modelInfo.textureSRVs.empty()) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID %d has no textures. Applying fallback texture.", m_modelInfo.ID);
            LoadFallbackTexture(); // creates and assigns fallback to m_modelInfo.textureSRVs
        }

        // Ensure at least one normal map SRV exists to prevent GPU crash
        if (m_modelInfo.normalMapSRVs.empty()) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID %d has no normal maps. Applying flat normal fallback.", m_modelInfo.ID);
            LoadFallbackNormalMap();
        }

        //=== SHADER SETUP ===//
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;

        std::filesystem::path vsPath = "ModelVShader.hlsl";
        std::filesystem::path psPath = "ModelPShader.hlsl";

        if (FAILED(CompileShaderFromFile(vsPath.c_str(), "main", "vs_5_0", &vsBlob))) return false;
        if (FAILED(CompileShaderFromFile(psPath.c_str(), "main", "ps_5_0", &psBlob))) return false;

        if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_modelInfo.vertexShader))) return false;
        if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_modelInfo.pixelShader))) return false;

        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, texCoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, tangent),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        HRESULT hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_modelInfo.inputLayout);
        if (FAILED(hr)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateInputLayout failed. HRESULT = 0x%08X", hr);
            return false;
        }

        //=== BUFFER SETUP ===//
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(m_modelInfo.vertices.size() * sizeof(Vertex));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = { m_modelInfo.vertices.data() };
        #if defined(_DEBUG_MODEL_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Vertex count: %d", (int)m_modelInfo.vertices.size());
        #endif
        if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &m_modelInfo.vertexBuffer))) return false;

        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = static_cast<UINT>(m_modelInfo.indices.size() * sizeof(uint32_t));
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData = { m_modelInfo.indices.data() };
        #if defined(_DEBUG_MODEL_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Index count: %d", (int)m_modelInfo.indices.size());
        #endif
        if (FAILED(device->CreateBuffer(&ibDesc, &ibData, &m_modelInfo.indexBuffer))) return false;

        //=== CONSTANT BUFFERS ===//
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(ConstantBuffer);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_modelInfo.constantBuffer))) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to create Constant Buffer.");
            return false;
        }

        D3D11_BUFFER_DESC lightDesc = {};
        lightDesc.ByteWidth = sizeof(LightBuffer);
        lightDesc.Usage = D3D11_USAGE_DYNAMIC;
        lightDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        lightDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&lightDesc, nullptr, &m_modelInfo.lightConstantBuffer))) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to create Light Buffer.");
            return false;
        }

        D3D11_BUFFER_DESC matDesc = {};
        matDesc.ByteWidth = sizeof(MaterialGPU);
        matDesc.Usage = D3D11_USAGE_DYNAMIC;
        matDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        matDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&matDesc, nullptr, m_modelInfo.materialBuffer.GetAddressOf()))) return false;

        //=== SAMPLER ===//
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampDesc, &m_modelInfo.samplerState))) return false;
        
        SetupPBRResources();

        return true;
        });

    return true;
#else
    return false;
#endif
}

// ============================================================================
// UpdateModelLighting() - Pushes local lights to GPU constant buffer (b1)
// ============================================================================
void Model::UpdateModelLighting()
{
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> dx11)
        {
            if (!m_modelInfo.lightConstantBuffer)
                return;

            LightBuffer buffer = {};
            int maxLights = std::min(static_cast<int>(m_modelInfo.localLights.size()), MAX_MODEL_LIGHTS);
            buffer.numLights = maxLights;

            for (int i = 0; i < maxLights; ++i)
            {
                buffer.lights[i] = m_modelInfo.localLights[i];
            }

            if (m_modelInfo.lightConstantBuffer)
            {
                D3D11_MAPPED_SUBRESOURCE mappedLight = {};
                HRESULT hr = dx11->m_d3dContext->Map(m_modelInfo.lightConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedLight);
                if (SUCCEEDED(hr))
                {
                    memcpy(mappedLight.pData, &buffer, sizeof(LightBuffer));
                    dx11->m_d3dContext->Unmap(m_modelInfo.lightConstantBuffer.Get(), 0);
                    dx11->m_d3dContext->PSSetConstantBuffers(SLOT_LIGHT_BUFFER, 1, m_modelInfo.lightConstantBuffer.GetAddressOf());


#if defined(_DEBUG_MODEL_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[Model] Lighting updated (%d lights)", buffer.numLights);
#endif
                }
            }
            else
            {
#if defined(_DEBUG_MODEL_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[Model] Failed to map light buffer for writing.");
#endif
            }

        });
}

//=============================================================================
// void Model::Render(ID3D11DeviceContext* deviceContext)
//=============================================================================
void Model::Render(ID3D11DeviceContext* deviceContext, float deltaTime)
{
#if defined(__USE_DIRECTX_11__)

    std::shared_ptr<DX11Renderer> dx11;
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> renderer) {
        dx11 = renderer;
        });

    if (!deviceContext || !threadManager.threadVars.bLoaderTaskFinished.load())
    {
#if defined(_DEBUG_MODEL_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_CRITICAL,
            L"Model ID: %d, Device=%p, Context=%p, Loading Completed=%s", 
            m_modelInfo.ID, 
            dx11 ? dx11->m_d3dContext.Get() : nullptr, 
            threadManager.threadVars.bLoaderTaskFinished.load() ? L"True" : L"False"); 
#endif
        return; 
    }

    if (!m_isLoaded || bIsDestroyed)
    {
#if defined(_DEBUG_MODEL_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID %d has FAILED SAFETY CHECK!", m_modelInfo.ID);
#endif
        return;
    }

    // Updates the model's animation state.
    UpdateAnimation(deltaTime);

    // Updates the constant buffer with the current world matrix.
    UpdateConstantBuffer();

    // Set the input layout and shaders
    deviceContext->IASetInputLayout(m_modelInfo.inputLayout.Get());
    deviceContext->VSSetShader(m_modelInfo.vertexShader.Get(), nullptr, 0);
    deviceContext->PSSetShader(m_modelInfo.pixelShader.Get(), nullptr, 0);

    // Set the vertex and index buffers
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    deviceContext->IASetVertexBuffers(0, 1, m_modelInfo.vertexBuffer.GetAddressOf(), &stride, &offset);
    deviceContext->IASetIndexBuffer(m_modelInfo.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind constant buffers
    deviceContext->VSSetConstantBuffers(SLOT_CONST_BUFFER, 1, m_modelInfo.constantBuffer.GetAddressOf());
    deviceContext->PSSetConstantBuffers(SLOT_CONST_BUFFER, 1, m_modelInfo.constantBuffer.GetAddressOf());

    // === Update Material Buffer (b4) ===
    if (m_modelInfo.materialBuffer)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = deviceContext->Map(m_modelInfo.materialBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            MaterialGPU* matGPU = reinterpret_cast<MaterialGPU*>(mappedResource.pData);
            if (!m_materials.empty())
            {
                const Material* mat = nullptr;
                if (!m_materials.empty()) {
                    mat = &(m_materials.begin()->second);
                }
                if (mat) {
                    matGPU->Ka = mat->Ka;
                    matGPU->Kd = mat->Kd;
                    matGPU->Ks = mat->Ks;
                    matGPU->Ns = mat->Ns;
                }
                else 
                {
                    // fallback material
                    matGPU->Ka = XMFLOAT3(0.1f, 0.1f, 0.1f);
                    matGPU->Kd = XMFLOAT3(0.8f, 0.8f, 0.8f);
                    matGPU->Ks = XMFLOAT3(1.0f, 1.0f, 1.0f);
                    matGPU->Ns = 16.0f;
                }
            }
            else
            {
                // Fallback material if no materials are defined
                matGPU->Ka = XMFLOAT3(0.1f, 0.1f, 0.1f);
                matGPU->Kd = XMFLOAT3(0.8f, 0.8f, 0.8f);
                matGPU->Ks = XMFLOAT3(1.0f, 1.0f, 1.0f);
                matGPU->Ns = 16.0f;
            }

            if (m_modelInfo.materialBuffer)
               deviceContext->Unmap(m_modelInfo.materialBuffer.Get(), 0);
        }

        // Bind b4 Materials to pixel shader
        if (m_modelInfo.materialBuffer)
           deviceContext->PSSetConstantBuffers(SLOT_MATERIAL_BUFFER, 1, m_modelInfo.materialBuffer.GetAddressOf());
    }

    // Apply fallback texture if missing
    if (m_modelInfo.textureSRVs.empty()) {
#if defined(_DEBUG_MODEL_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID %d has no textures. Applying fallback texture.", m_modelInfo.ID);
#endif
        LoadFallbackTexture();
    }

    if (m_modelInfo.normalMapSRVs.empty()) {
#if defined(_DEBUG_MODEL_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Model ID %d has no normal maps. Applying flat normal fallback.", m_modelInfo.ID);
#endif
        LoadFallbackNormalMap();
    }

    // Bind texture SRVs to pixel shader slots
    ID3D11ShaderResourceView* texSRV = m_modelInfo.textureSRVs.empty() ? nullptr : m_modelInfo.textureSRVs[0].Get();
    ID3D11ShaderResourceView* normSRV = m_modelInfo.normalMapSRVs.empty() ? nullptr : m_modelInfo.normalMapSRVs[0].Get();
    ID3D11ShaderResourceView* metalMapSRV = m_modelInfo.metallicMapSRV ? nullptr : m_modelInfo.metallicMapSRV.Get();
    ID3D11ShaderResourceView* roughMapSRV = m_modelInfo.roughnessMapSRV ? nullptr : m_modelInfo.roughnessMapSRV.Get();
    ID3D11ShaderResourceView* aoMapSRV = m_modelInfo.aoMapSRV ? nullptr : m_modelInfo.aoMapSRV.Get();
    ID3D11ShaderResourceView* enviroMapSRV = m_modelInfo.environmentMapSRV ? nullptr : m_modelInfo.environmentMapSRV.Get();

    if (texSRV)
        deviceContext->PSSetShaderResources(SLOT_diffuseTexture, 1, &texSRV);

    if (normSRV)
        deviceContext->PSSetShaderResources(SLOT_normalMap, 1, &normSRV);

    if (metalMapSRV)
        deviceContext->PSSetShaderResources(SLOT_metallicMap, 1, &metalMapSRV);

    if (roughMapSRV)
        deviceContext->PSSetShaderResources(SLOT_roughnessMap, 1, &roughMapSRV);

    if (aoMapSRV)
        deviceContext->PSSetShaderResources(SLOT_aoMap, 1, &aoMapSRV);

    if (enviroMapSRV)
        deviceContext->PSSetShaderResources(SLOT_environmentMap, 1, &enviroMapSRV);

    deviceContext->PSSetSamplers(SLOT_SAMPLER_STATE, 1, m_modelInfo.samplerState.GetAddressOf());
    deviceContext->PSSetSamplers(SLOT_ENVIRO_SAMPLER_STATE, 1, m_modelInfo.environmentSamplerState.GetAddressOf());

    // Update model-specific lighting parameters.
    UpdateModelLighting();

#if defined(_DEBUG_MODEL_RENDERER_) && defined(_DEBUG)
    DebugInfoForModel();
#endif

    // Render the model
    deviceContext->DrawIndexed(static_cast<UINT>(m_modelInfo.indices.size()), 0, 0);

#endif
}

void Model::DebugInfoForModel() const
{
#if defined(_DEBUG_MODEL_RENDERER_) && defined(_DEBUG)
    const ModelInfo& info = m_modelInfo;

    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[MODEL DEBUG] ID=%d | Name=%ls", info.ID, info.name.c_str());

    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[POSITION] X=%.2f Y=%.2f Z=%.2f", info.position.x, info.position.y, info.position.z);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SCALE]    X=%.2f Y=%.2f Z=%.2f", info.scale.x, info.scale.y, info.scale.z);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ROTATION] X=%.2f Y=%.2f Z=%.2f", info.rotation.x, info.rotation.y, info.rotation.z);

    // Dump worldMatrix via XMStoreFloat4x4
    XMFLOAT4X4 matOut = {};
    XMStoreFloat4x4(&matOut, info.worldMatrix);

    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WORLD MATRIX]");
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L" %.2f %.2f %.2f %.2f", matOut._11, matOut._12, matOut._13, matOut._14);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L" %.2f %.2f %.2f %.2f", matOut._21, matOut._22, matOut._23, matOut._24);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L" %.2f %.2f %.2f %.2f", matOut._31, matOut._32, matOut._33, matOut._34);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L" %.2f %.2f %.2f %.2f", matOut._41, matOut._42, matOut._43, matOut._44);

    // Geometry
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GEOMETRY] Vertices = %zu | Indices = %zu", info.vertices.size(), info.indices.size());
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[LOCAL LIGHTS] Count = %zu", info.localLights.size());

    // Materials
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[MATERIALS] %zu entries", m_materials.size());
    int i = 0;
    for (const auto& [name, mat] : m_materials)
    {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  [%d] Name: %hs", i, name.c_str());
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"      DiffuseMap: %hs | NormalMap: %hs",
            mat.diffuseMapPath.c_str(), mat.normalMapPath.c_str());
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"      Kd: %.2f %.2f %.2f | Ks: %.2f %.2f %.2f | Ns=%.2f",
            mat.Kd.x, mat.Kd.y, mat.Kd.z,
            mat.Ks.x, mat.Ks.y, mat.Ks.z,
            mat.Ns);
        ++i;
    }
#endif
}

// Implementation for Model class PBR extension methods
bool Model::SetupPBRResources() {
    // Get DX11 device
    std::shared_ptr<DX11Renderer> dx11;
    if (!dx11)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get DX11 renderer for PBR setup");
        return false;
    }

    ComPtr<ID3D11Device> device = dx11->m_d3dDevice;
    if (!device) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid D3D11 device in SetupPBRResources");
        return false;
    }

    // Create environment buffer
    D3D11_BUFFER_DESC envBufferDesc = {};
    envBufferDesc.ByteWidth = sizeof(EnvBufferGPU);
    envBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    envBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    envBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&envBufferDesc, nullptr, &m_modelInfo.environmentBuffer);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create environment buffer");
        return false;
    }

    // Create environment sampler state
    D3D11_SAMPLER_DESC envSamplerDesc = {};
    envSamplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    envSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    envSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    envSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    envSamplerDesc.MaxAnisotropy = 16;
    envSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    envSamplerDesc.MinLOD = 0;
    envSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&envSamplerDesc, &m_modelInfo.environmentSamplerState);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create environment sampler state");
        return false;
    }

    // Set up default environment settings
    SetEnvironmentProperties(1.0f, XMFLOAT3(1.0f, 1.0f, 1.0f), 0.0f, 0.04f);

    return true;
}

bool Model::LoadEnvironmentMap(const std::wstring& filePath) {
    // Get DX11 device
    std::shared_ptr<DX11Renderer> dx11;
    if (!dx11)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get DX11 renderer for environment map loading");
        return false;
    }

    ComPtr<ID3D11Device> device = dx11->m_d3dDevice;
    if (!device) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid D3D11 device in LoadEnvironmentMap");
        return false;
    }

    // Ensure file exists
    if (!std::filesystem::exists(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Environment map file not found: " + filePath);
        return false;
    }

    // Create DirectX Texture from DDS file (cube map)
    ComPtr<ID3D11Resource> texResource;
/* 
    HRESULT hr = DirectX::CreateDDSTextureFromFileEx(
        device.Get(),
        filePath.c_str(),
        0,  // maxSize
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE,
        0,  // cpuFlags
        D3D11_RESOURCE_MISC_TEXTURECUBE,  // miscFlags - specify this is a cube map
        false,  // forceSRGB
        &texResource,
        &m_modelInfo.environmentMapSRV
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load environment cube map: " + filePath);
        return false;
    }
*/
    m_modelInfo.useEnvironmentMap = true;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully loaded environment cube map: " + filePath);
    return true;
}

bool Model::LoadMetallicMap(const std::wstring& filePath) {
    auto tex = std::make_shared<Texture>();
    if (!tex->LoadFromFile(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load metallic map: " + filePath);
        return false;
    }

    m_modelInfo.metallicMap = tex;
    m_modelInfo.metallicMapSRV = tex->GetSRV();
    m_modelInfo.useMetallicMap = true;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully loaded metallic map: " + filePath);
    return true;
}

bool Model::LoadRoughnessMap(const std::wstring& filePath) {
    auto tex = std::make_shared<Texture>();
    if (!tex->LoadFromFile(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load roughness map: " + filePath);
        return false;
    }

    m_modelInfo.roughnessMap = tex;
    m_modelInfo.roughnessMapSRV = tex->GetSRV();
    m_modelInfo.useRoughnessMap = true;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully loaded roughness map: " + filePath);
    return true;
}

bool Model::LoadAOMap(const std::wstring& filePath) {
    auto tex = std::make_shared<Texture>();
    if (!tex->LoadFromFile(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load ambient occlusion map: " + filePath);
        return false;
    }

    m_modelInfo.aoMap = tex;
    m_modelInfo.aoMapSRV = tex->GetSRV();
    m_modelInfo.useAOMap = true;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully loaded ambient occlusion map: " + filePath);
    return true;
}

void Model::UpdateEnvironmentBuffer() {
    // Get DX11 device
    std::shared_ptr<DX11Renderer> dx11;
    if (!dx11)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get DX11 renderer for environment buffer update");
        return;
    }

    ComPtr<ID3D11DeviceContext> context = dx11->m_d3dContext;
    if (!context || !m_modelInfo.environmentBuffer) {
        return;
    }

    // Update environment buffer
    EnvBufferGPU envData = {};
    envData.envIntensity = m_modelInfo.envIntensity;
    envData.envTint = m_modelInfo.envTint;
    envData.mipLODBias = m_modelInfo.mipLODBias;
    envData.fresnel0 = m_modelInfo.fresnel0;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = context->Map(m_modelInfo.environmentBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        memcpy(mappedResource.pData, &envData, sizeof(EnvBufferGPU));
        context->Unmap(m_modelInfo.environmentBuffer.Get(), 0);

        // Bind to Pixel Shader slot b5
        context->PSSetConstantBuffers(SLOT_ENVIRONMENT_BUFFER, 1, m_modelInfo.environmentBuffer.GetAddressOf());
    }
}

void Model::SetPBRProperties(float metallic, float roughness, float reflectionStrength) {
    m_modelInfo.metallic = metallic;
    m_modelInfo.roughness = roughness;
    m_modelInfo.reflectionStrength = reflectionStrength;

    // Update material values in model materials if they exist
    for (auto& [name, mat] : m_materials) {
        mat.Metallic = metallic;
        mat.Roughness = roughness;
        mat.Reflection = reflectionStrength;
    }
}

void Model::SetEnvironmentProperties(float intensity, XMFLOAT3 tint, float mipBias, float fresnel0) {
    m_modelInfo.envIntensity = intensity;
    m_modelInfo.envTint = tint;
    m_modelInfo.mipLODBias = mipBias;
    m_modelInfo.fresnel0 = fresnel0;
}


