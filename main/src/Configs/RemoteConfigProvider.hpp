#pragma once

#include <string>
#include <vector>

struct RemoteProfileResult {
    bool success = false;
    std::string error;
    std::string productId;
    std::string profileUrl;
    std::string profileVersion;
    std::string checksum;
    std::string cachedPath;
};

struct ProductInfo {
    std::string id;
    std::string name;
    std::string module;
    std::string bus;
    std::string profileUrl;
    std::string version;
};

class RemoteConfigProvider {
public:
    static constexpr const char* kDefaultManifestUrl =
        "https://raw.githubusercontent.com/opencomfort/configs/main/manifest.json";
    static constexpr const char* kBundledManifestPath = "/spiffs/manifest.json";

    explicit RemoteConfigProvider(
        std::string manifestUrl = kDefaultManifestUrl
    );

    bool loadManifest(std::string& manifest, std::string* error = nullptr) const;
    bool listProducts(std::vector<ProductInfo>& products, std::string* error = nullptr) const;
    RemoteProfileResult fetchAndCacheProfile(const std::string& productId);

private:
    std::string manifestUrl_;

    bool fetchRemoteUrl(const std::string& url, std::string& body, int maxBytes, std::string& error) const;
    bool fetchContent(const std::string& url, std::string& body, int maxBytes, std::string& error) const;
    bool resolveProfileFromManifest(
        const std::string& manifest,
        const std::string& productId,
        RemoteProfileResult& result
    ) const;
    bool validateProfileJson(const std::string& profileJson, std::string& error) const;
    bool parseProducts(const std::string& manifest, std::vector<ProductInfo>& products, std::string& error) const;
};
