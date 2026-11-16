#pragma once
#include <windows.h>
#include <expected>
#include <string>
#include <utility>
#include <cstdio>
namespace arcade_ui {
struct Error {
    HRESULT code=S_OK;
    std::wstring message;
    static Error FromHresult(HRESULT hr, std::wstring context) { return {hr,std::move(context)}; }
    std::string DescribeUtf8() const {
        char prefix[32]; std::snprintf(prefix,sizeof(prefix),"[0x%08lX] ",static_cast<unsigned long>(code));
        const int size=WideCharToMultiByte(CP_UTF8,0,message.data(),static_cast<int>(message.size()),nullptr,0,nullptr,nullptr);
        std::string result(prefix); const auto offset=result.size(); result.resize(offset+size);
        if (size) WideCharToMultiByte(CP_UTF8,0,message.data(),static_cast<int>(message.size()),result.data()+offset,size,nullptr,nullptr);
        return result;
    }
};
template<class T> using Result=std::expected<T,Error>;
}
