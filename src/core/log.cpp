// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/log.h"

#include "sp2/build_info.h"

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

namespace sp2::core
{
    namespace
    {
        bool SamePathCharacter(char a, char b)
        {
            const auto separator = [](char c) { return c == '\\' || c == '/'; };
            const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
            return separator(a) ? separator(b) : lower(a) == lower(b);
        }

        // SkyrimPlatform2.log becomes SkyrimPlatform2.<index>.log.
        std::filesystem::path OlderFile(const std::filesystem::path& file, std::size_t index)
        {
            auto name = file.stem();
            name += std::format(".{}", index);
            name += file.extension();
            return file.parent_path() / name;
        }

        // spdlog's file sinks open narrow file names in the ANSI code page; std::ofstream takes the wide path.
        class LogFileSink final : public spdlog::sinks::base_sink<std::mutex>
        {
        public:
            LogFileSink(std::filesystem::path file, std::string profile, LogOptions options) :
                _file(std::move(file)),
                _profile(std::move(profile)),
                _options(options)
            {
                std::error_code error;
                if (std::filesystem::file_size(_file, error) > 0 && !error) {
                    Rotate();
                }
                Open();
            }

            [[nodiscard]] bool IsOpen() const { return _stream.is_open(); }

        protected:
            void sink_it_(const spdlog::details::log_msg& message) override
            {
                const std::string payload = ConfineToEntry(RedactProfile({ message.payload.data(), message.payload.size() }, _profile));
                auto redacted = message;
                redacted.payload = payload;
                spdlog::memory_buf_t line;
                formatter_->format(redacted, line);

                if (_size > 0 && _size + line.size() > _options.maxFileBytes) {
                    Rotate();
                    Open();
                }
                _stream.write(line.data(), static_cast<std::streamsize>(line.size()));
                _stream.flush();
                _size += line.size();
            }

            void flush_() override { _stream.flush(); }

        private:
            void Open()
            {
                _stream.open(_file, std::ios::out | std::ios::binary | std::ios::trunc);
                _size = 0;
            }

            // Older files are best effort: a failed remove or rename never stops logging.
            void Rotate()
            {
                _stream.close();
                std::error_code ignored;
                if (_options.olderFiles == 0) {
                    return;
                }
                std::filesystem::remove(OlderFile(_file, _options.olderFiles), ignored);
                for (std::size_t index = _options.olderFiles; index > 1; --index) {
                    std::filesystem::rename(OlderFile(_file, index - 1), OlderFile(_file, index), ignored);
                }
                std::filesystem::rename(_file, OlderFile(_file, 1), ignored);
            }

            std::filesystem::path _file;
            std::string _profile;
            LogOptions _options;
            std::ofstream _stream;
            std::size_t _size{ 0 };
        };
    }

    std::string StartupLine()
    {
        return std::format("{} {} ({} build)", build::kProductName, build::kVersion, build::kDevBuild ? "dev" : "release");
    }

    std::string RedactProfile(std::string_view text, std::string_view profile)
    {
        constexpr std::string_view kRedacted = "%USERPROFILE%";
        if (profile.empty()) {
            return std::string(text);
        }
        std::string result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (text.size() - i >= profile.size() && std::ranges::equal(text.substr(i, profile.size()), profile, SamePathCharacter)) {
                result += kRedacted;
                i += profile.size();
            } else {
                result += text[i++];
            }
        }
        return result;
    }

    std::string ConfineToEntry(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == '\n') {
                result += "\n    ";
            } else if ((byte < 0x20 && c != '\t') || byte == 0x7F) {
                result += std::format("\\x{:02X}", byte);
            } else {
                result += c;
            }
        }
        return result;
    }

    std::shared_ptr<spdlog::logger> OpenLog(const std::filesystem::path& directory, const std::filesystem::path& profile, LogOptions options)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return nullptr;
        }
        const auto utf8 = profile.u8string();
        auto sink = std::make_shared<LogFileSink>(directory / kLogFileName, std::string(utf8.begin(), utf8.end()), options);
        if (!sink->IsOpen()) {
            return nullptr;
        }
        auto logger = std::make_shared<spdlog::logger>(std::string(build::kProductName), std::move(sink));
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->set_level(spdlog::level::info);
        return logger;
    }
}
