#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <fmt/format.h>

#include <hlp/base.hpp>
#include <hlp/hlp.hpp>
#include <hlp/parsec.hpp>
#include <json/json.hpp>

namespace hlp
{

parsec::Parser<json::Json> getUriParser(Stop endTokens, Options lst)
{
    if (endTokens.empty())
    {
        throw std::invalid_argument(fmt::format("Uri parser needs a stop string"));
    }

    if (lst.size() > 0)
    {
        throw std::runtime_error(fmt::format("URL parser do not accept arguments!"));
    }

    std::map<CURLUPart, std::string_view> mapCurlFields = {
        {CURLUPART_URL, "/original"},
        {CURLUPART_HOST, "/domain"},
        {CURLUPART_PATH, "/path"},
        {CURLUPART_SCHEME, "/scheme"},
        {CURLUPART_USER, "/username"},
        {CURLUPART_PASSWORD, "/password"},
        {CURLUPART_PORT, "/port"},
        {CURLUPART_QUERY, "/query"},
        {CURLUPART_FRAGMENT, "/fragment"},
        // {CURLUPART_OPTIONS, "/options"}
    };

    return [endTokens, mapCurlFields = std::move(mapCurlFields)](std::string_view text,
                                                                 int index)
    {
        auto res = internal::preProcess<json::Json>(text, index, endTokens);
        if (std::holds_alternative<parsec::Result<json::Json>>(res))
        {
            return std::get<parsec::Result<json::Json>>(res);
        }

        auto urlStr = std::string{std::get<std::string_view>(res)};

        auto urlCleanup = [](auto* url)
        {
            curl_url_cleanup(url);
        };
        std::unique_ptr<CURLU, decltype(urlCleanup)> url {curl_url(), urlCleanup};

        if (url.get() == nullptr)
        {
            return parsec::makeError<json::Json>(
                fmt::format("Unable to initialize the url container"), index);
        }


        auto uc = curl_url_set(url.get(), CURLUPART_URL, urlStr.c_str(), 0);
        if (uc)
        {
            return parsec::makeError<json::Json>(
                fmt::format("error parsing url"), index);
        }

        // TODO curl will parse and copy the URL into an allocated
        // char ptr and we will copy it again into the string for the result
        // Check if there's a way to avoid all the copying here

        // Load the fild values into the json document
        json::Json doc {};
        auto load = [&doc, &url](CURLUPart field, std::string_view path)
        {
            char* str = nullptr;
            auto uc = curl_url_get(url.get(), field, &str, 0);
            if (uc == CURLUE_OK)
            {
                doc.setString(std::string {str}, path);
                curl_free(str);
            }
        };

        for (auto& [field, path] : mapCurlFields)
        {
            load(field, path);
        }
        // TODO Check if urlStr.size() == doc["original"].size()
        return parsec::makeSuccess<json::Json>(std::move(doc), urlStr.size() + index);
    };
}

parsec::Parser<json::Json> getUAParser(Stop endTokens, Options lst)
{
    if (endTokens.empty())
    {
        throw std::invalid_argument(fmt::format("User-agent parser needs a stop string"));
    }

    if (lst.size() != 0)
    {
        throw std::runtime_error(fmt::format("URL parser do not accept arguments!"));
    }

    return [endTokens](std::string_view text, int index)
    {
        auto res = internal::preProcess<json::Json>(text, index, endTokens);
        if (std::holds_alternative<parsec::Result<json::Json>>(res))
        {
            return std::get<parsec::Result<json::Json>>(res);
        }

        auto fp = std::get<std::string_view>(res);
        auto pos = fp.size() + index;

        json::Json doc;
        doc.setString(std::string {fp}, "/user_agent/original");

        return parsec::makeSuccess<json::Json>(std::move(doc), pos);
    };
}

// We validate it is a valid FQDN or PQDN we do not
// extract any component here.
parsec::Parser<json::Json> getFQDNParser(Stop endTokens, Options lst)
{

    return [endTokens](std::string_view text, int index)
    {
        auto res = internal::preProcess<json::Json>(text, index, endTokens);
        if (std::holds_alternative<parsec::Result<json::Json>>(res))
        {
            return std::get<parsec::Result<json::Json>>(res);
        }

        auto fp = std::get<std::string_view>(res);
        auto pos = fp.size() + index;

        if (fp.size() > 253)
            return parsec::makeError<json::Json>(
                fmt::format("size '{}' too big for an fqdn", fp.size()), index);

        char last = 0;
        auto e = std::find_if_not(fp.begin(),
                                  fp.end(),
                                  [&last](char c) mutable
                                  {
                                      auto r = (std::isalnum(c) || c == '-')
                                               || (c == '.' && c != last);
                                      last = c;
                                      return r;
                                  });
        json::Json doc;
        if (e == fp.end())
        {
            doc.setString(fp.data());
            return parsec::makeSuccess<json::Json>(std::move(doc), pos);
        }
        return parsec::makeError<json::Json>(
            fmt::format("invalid char '{}' found while parsing", e[0]), index);
    };
}

} // namespace hlp
