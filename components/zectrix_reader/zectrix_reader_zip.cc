#include "reader_internal.h"

#include <algorithm>
#include <cstring>

namespace zectrix::reader::detail {

uint16_t Le16(const uint8_t* p) { return p[0] | static_cast<uint16_t>(p[1]) << 8; }
uint32_t Le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 |
        static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

namespace {
uint32_t ZipCrc(const uint8_t* bytes, std::size_t size, uint32_t previous) {
    uint32_t crc = ~previous;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

}  // namespace

Result Zip::Open(Source& source) {
    source_ = nullptr;
    if (source.Size() < 22) return Result::Invalid;
    // Search the complete legal ZIP comment window without allocating it.
    std::array<uint8_t, 1024 + 21> window{};
    uint32_t end = source.Size();
    const uint32_t first = source.Size() > 65557 ? source.Size() - 65557 : 0;
    while (end > first) {
        const uint32_t begin = end - std::min<uint32_t>(1024, end - first);
        const uint32_t length = std::min<uint32_t>(source.Size() - begin, end - begin + 21);
        if (!source.Read(begin, window.data(), length)) return Result::IoError;
        for (uint32_t i = end - begin; i-- > 0;) {
            if (i + 22 > length || Le32(window.data() + i) != 0x06054b50) continue;
            const auto* p = window.data() + i;
            if (static_cast<uint64_t>(begin) + i + 22 + Le16(p + 20) != source.Size()) continue;
            if (Le16(p + 4) || Le16(p + 6) || Le16(p + 8) != Le16(p + 10) ||
                Le16(p + 10) == 0xffff || Le32(p + 12) == UINT32_MAX ||
                Le32(p + 16) == UINT32_MAX) return Result::Unsupported;
            entries_ = Le16(p + 10);
            directory_ = Le32(p + 16);
            const uint64_t directory_end = static_cast<uint64_t>(directory_) + Le32(p + 12);
            if (entries_ == 0 || directory_end != begin + i) return Result::Invalid;
            directory_end_ = static_cast<uint32_t>(directory_end);
            source_ = &source;
            return Result::Ok;
        }
        end = begin;
    }
    return Result::Invalid;
}

Result Zip::Find(const char* path) {
    if (!source_ || !path || !*path || std::strlen(path) >= path_.size()) return Result::Invalid;
    std::strcpy(path_.data(), path);
    found_ = false;
    index_ = 0;
    offset_ = directory_;
    return Result::Pending;
}

Result Zip::Poll(Entry* entry, std::size_t* budget) {
    if (!source_ || !entry || !budget) return Result::Invalid;
    const auto path_size = std::strlen(path_.data());
    std::array<uint8_t, 46> header{};
    std::array<char, kPathCapacity> name{};
    while (*budget && index_ < entries_) {
        if (offset_ > directory_end_ || directory_end_ - offset_ < header.size()) return Result::Invalid;
        if (!source_->Read(offset_, header.data(), header.size())) return Result::IoError;
        const auto* p = header.data();
        if (Le32(p) != 0x02014b50) return Result::Invalid;
        const uint32_t name_size = Le16(p + 28);
        const uint32_t length = 46 + name_size + Le16(p + 30) + Le16(p + 32);
        if (length > directory_end_ - offset_) return Result::Invalid;
        const std::size_t cost = header.size() + (name_size == path_size ? name_size : 0);
        *budget -= std::min(*budget, cost);
        if (name_size == path_size && name_size < name.size()) {
            if (!source_->Read(offset_ + 46, name.data(), name_size)) return Result::IoError;
            if (std::memcmp(name.data(), path_.data(), path_size) == 0) {
                if (found_) return Result::Invalid;
                found_entry_ = {Le32(p + 42), Le32(p + 20), Le32(p + 24),
                          Le32(p + 16), Le16(p + 10), Le16(p + 8)};
                if (Le16(p + 34) || found_entry_.local == UINT32_MAX ||
                    found_entry_.size == UINT32_MAX || found_entry_.compressed == UINT32_MAX)
                    return Result::Unsupported;
                found_ = true;
            }
        }
        offset_ += length;
        ++index_;
    }
    if (index_ != entries_) return Result::Pending;
    if (offset_ != directory_end_) return Result::Invalid;
    if (found_) *entry = found_entry_;
    return found_ ? Result::Ok : Result::End;
}

Result Stream::Open(Source& source, const Entry& entry, bool zipped, uint32_t text_offset) {
    if (text_offset > entry.size || (zipped && text_offset)) return Result::Invalid;
    source_ = &source;
    entry_ = entry;
    zipped_ = zipped;
    input_loaded_ = produced_ = consumed_ = crc_ = 0;
    if (!zipped) produced_ = consumed_ = text_offset;
    input_pos_ = input_size_ = output_pos_ = output_end_ = 0;
    done_ = false;
    data_offset_ = 0;
    if (zipped) {
        if ((entry.flags & ~uint16_t{0x080e}) || (entry.method != 0 && entry.method != 8))
            return Result::Unsupported;
        if (entry.size > kChapterLimit) return Result::TooLarge;
        std::array<uint8_t, 30> header{};
        if (entry.local > source.Size() || source.Size() - entry.local < header.size()) return Result::Invalid;
        if (!source.Read(entry.local, header.data(), header.size())) return Result::IoError;
        if (Le32(header.data()) != 0x04034b50 || Le16(header.data() + 8) != entry.method ||
            Le16(header.data() + 6) != entry.flags) return Result::Invalid;
        const uint64_t start = static_cast<uint64_t>(entry.local) + 30 +
            Le16(header.data() + 26) + Le16(header.data() + 28);
        if (start + entry.compressed > source.Size()) return Result::Invalid;
        if (!(entry.flags & 8) && (Le32(header.data() + 14) != entry.crc ||
            Le32(header.data() + 18) != entry.compressed || Le32(header.data() + 22) != entry.size))
            return Result::Invalid;
        data_offset_ = static_cast<uint32_t>(start);
    }
    if (entry.method == 0 && entry.compressed != entry.size) return Result::Invalid;
    tinfl_init(&inflater_);
    return Result::Ok;
}

Result Stream::Fill() {
    if (done_) return Result::End;
    if (entry_.method == 0) {
        const auto size = std::min<std::size_t>(input_.size(), entry_.size - produced_);
        if (size && !source_->Read(data_offset_ + produced_, dictionary_.data(), size)) return Result::IoError;
        output_pos_ = 0;
        output_end_ = size;
        produced_ += size;
        if (zipped_) crc_ = ZipCrc(dictionary_.data(), size, crc_);
        done_ = produced_ == entry_.size;
    } else {
        output_pos_ = output_end_ % dictionary_.size();
        output_end_ = output_pos_;
        {
            if (input_pos_ == input_size_) {
                input_size_ = std::min<std::size_t>(input_.size(), entry_.compressed - input_loaded_);
                input_pos_ = 0;
                if (input_size_ && !source_->Read(data_offset_ + input_loaded_, input_.data(), input_size_))
                    return Result::IoError;
                input_loaded_ += input_size_;
            }
            std::size_t input_bytes = input_size_ - input_pos_;
            std::size_t output_bytes = dictionary_.size() - output_end_;
            const auto status = tinfl_decompress(&inflater_, input_.data() + input_pos_, &input_bytes,
                dictionary_.data(), dictionary_.data() + output_end_, &output_bytes,
                input_loaded_ < entry_.compressed ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            input_pos_ += input_bytes;
            if (status < TINFL_STATUS_DONE || output_bytes > entry_.size - produced_) return Result::Invalid;
            crc_ = ZipCrc(dictionary_.data() + output_end_, output_bytes, crc_);
            produced_ += output_bytes;
            output_end_ += output_bytes;
            done_ = status == TINFL_STATUS_DONE;
            if (done_ && (produced_ != entry_.size || input_loaded_ - input_size_ + input_pos_ != entry_.compressed))
                return Result::Invalid;
            if (!done_ && !input_bytes && !output_bytes) return Result::Invalid;
        }
    }
    if (done_ && zipped_ && crc_ != entry_.crc) return Result::Invalid;
    return output_pos_ != output_end_ ? Result::Ok : done_ ? Result::End : Result::Pending;
}

Result Stream::Byte(uint8_t* output) {
    if (output_pos_ == output_end_) {
        const auto result = Fill();
        if (result != Result::Ok) return result;
    }
    *output = dictionary_[output_pos_++];
    ++consumed_;
    return Result::Ok;
}

Result Book::Open(Source& input, Format input_format) {
    source = &input;
    format = input_format;
    count = 0;
    total = 0;
    sections.fill({});
    package_path_.fill(0);
    xml_.Reset();
    in_spine_ = in_manifest_ = false;
    mime_offset_ = 0;
    phase_ = Phase::Ready;
    if (format == Format::Text) {
        sections[0].entry = {0, input.Size(), input.Size(), 0, 0, 0};
        count = 1;
        total = input.Size();
        return Result::Ok;
    }
    const auto result = zip_.Open(input);
    if (result != Result::Ok) return result;
    phase_ = Phase::MimeLookup;
    return zip_.Find("mimetype");
}

Result Book::Tag(const char* tag) {
    if (phase_ == Phase::Container) {
        if (!TagIs(tag, "rootfile") || package_path_[0]) return Result::Ok;
        char href[kPathCapacity]{}, type[64]{};
        if (!Attribute(tag, "media-type", type, sizeof(type)) ||
            std::strcmp(type, "application/oebps-package+xml") != 0) return Result::Ok;
        if (!Attribute(tag, "full-path", href, sizeof(href)) ||
            !ResolvePath("", href, package_path_.data(), package_path_.size())) return Result::Invalid;
    } else if (phase_ == Phase::Spine) {
        if (TagIs(tag, "spine")) in_spine_ = true;
        if (TagIs(tag, "spine", true)) in_spine_ = false;
        if (in_spine_ && TagIs(tag, "itemref")) {
            char linear[8]{};
            if (Attribute(tag, "linear", linear, sizeof(linear)) && std::strcmp(linear, "no") == 0)
                return Result::Ok;
            if (count == sections.size()) return Result::TooLarge;
            if (!Attribute(tag, "idref", sections[count].id.data(), sections[count].id.size()) ||
                !sections[count].id[0]) return Result::Invalid;
            ++count;
        }
    } else if (phase_ == Phase::Manifest) {
        if (TagIs(tag, "manifest")) in_manifest_ = true;
        if (TagIs(tag, "manifest", true)) in_manifest_ = false;
        if (!in_manifest_ || !TagIs(tag, "item")) return Result::Ok;
        char id[64]{};
        if (!Attribute(tag, "id", id, sizeof(id))) return Result::Invalid;
        for (uint16_t i = 0; i < count; ++i) {
            if (std::strcmp(id, sections[i].id.data()) != 0) continue;
            if (sections[i].found) return Result::Invalid;
            char type[64]{}, href[kPathCapacity]{}, resolved[kPathCapacity]{};
            if (!Attribute(tag, "media-type", type, sizeof(type)) ||
                std::strcmp(type, "application/xhtml+xml") != 0) return Result::Unsupported;
            if (!Attribute(tag, "href", href, sizeof(href)) ||
                !ResolvePath(package_path_.data(), href, resolved, sizeof(resolved))) return Result::Invalid;
            resolving_ = i;
            phase_ = Phase::ChapterLookup;
            return zip_.Find(resolved);
        }
    }
    return Result::Ok;
}

Result Book::EndMetadata() {
    if (!xml_.complete()) return Result::Invalid;
    xml_.Reset();
    if (phase_ == Phase::Container) {
        if (!package_path_[0]) return Result::Invalid;
        phase_ = Phase::PackageLookup;
        return zip_.Find(package_path_.data());
    }
    if (phase_ == Phase::Spine) {
        if (!count) return Result::Invalid;
        phase_ = Phase::Manifest;
        return stream.Open(*source, package_, true);
    }
    for (uint16_t i = 0; i < count; ++i) if (!sections[i].found) return Result::Invalid;
    phase_ = Phase::Ready;
    return Result::Ok;
}

Result Book::PollOpen(std::size_t budget) {
    while (budget && phase_ != Phase::Ready) {
        if (phase_ == Phase::EncryptionLookup) {
            Entry entry;
            const auto found = zip_.Poll(&entry, &budget);
            if (found == Result::Ok) return Result::Unsupported;
            if (found != Result::End) return found;
            phase_ = Phase::ContainerLookup;
            const auto lookup = zip_.Find("META-INF/container.xml");
            if (lookup != Result::Pending) return lookup;
            continue;
        }
        if (phase_ == Phase::MimeLookup || phase_ == Phase::ContainerLookup ||
            phase_ == Phase::PackageLookup || phase_ == Phase::ChapterLookup) {
            Entry entry;
            const auto found = zip_.Poll(&entry, &budget);
            if (found != Result::Ok) return found == Result::End ? Result::Invalid : found;
            if (phase_ == Phase::ChapterLookup) {
                if (entry.size > kChapterLimit) return Result::TooLarge;
                for (uint16_t i = 0; i < count; ++i) {
                    if (sections[i].id != sections[resolving_].id) continue;
                    sections[i].entry = entry;
                    sections[i].found = true;
                    total += entry.size;
                }
                phase_ = Phase::Manifest;
                continue;
            }
            if (phase_ == Phase::MimeLookup && entry.size != 20) return Result::Invalid;
            if (entry.size > kMetadataLimit) return Result::TooLarge;
            const auto opened = stream.Open(*source, entry, true);
            if (opened != Result::Ok) return opened;
            if (phase_ == Phase::MimeLookup) phase_ = Phase::Mime;
            else if (phase_ == Phase::ContainerLookup) phase_ = Phase::Container;
            else { package_ = entry; phase_ = Phase::Spine; }
            continue;
        }
        --budget;
        uint8_t byte = 0;
        const auto offset = stream.offset();
        const auto read = stream.Byte(&byte);
        if (phase_ == Phase::Mime) {
            constexpr char mime[] = "application/epub+zip";
            if (read != Result::Ok) return read == Result::End ? Result::Invalid : read;
            if (byte != static_cast<uint8_t>(mime[mime_offset_++])) return Result::Invalid;
            if (mime_offset_ == 20) {
                phase_ = Phase::EncryptionLookup;
                const auto lookup = zip_.Find("META-INF/encryption.xml");
                if (lookup != Result::Pending) return lookup;
            }
            continue;
        }
        if (read == Result::End) {
            const auto ended = EndMetadata();
            if (ended != Result::Ok && ended != Result::Pending) return ended;
            continue;
        }
        if (read != Result::Ok) return read;
        XmlEvent event;
        const auto parsed = xml_.Feed(byte, offset, &event);
        if (parsed != Result::Ok) return parsed;
        if (event.kind == XmlEvent::Kind::Tag) {
            const auto tag = Tag(xml_.tag());
            if (tag != Result::Ok && tag != Result::Pending) return tag;
        }
    }
    return phase_ == Phase::Ready ? Result::Ok : Result::Pending;
}

Result Book::Start(uint16_t chapter, uint32_t text_offset) {
    if (!source || chapter >= count) return Result::Invalid;
    return stream.Open(*source, sections[chapter].entry, format == Format::Epub, text_offset);
}

}  // namespace zectrix::reader::detail
