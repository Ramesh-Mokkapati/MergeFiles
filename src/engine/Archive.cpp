// Archive.cpp - comparison archives as XML (file contents as base64, so every byte is kept).
#include "Archive.h"

#include "Encoding.h"
#include "Xml.h"

namespace mf
{
    namespace
    {
        const wchar_t* EncodingKey(EncodingType type)
        {
            switch (type)
            {
            case EncodingType::Utf16LE:
                return L"utf16le";
            case EncodingType::Utf16BE:
                return L"utf16be";
            case EncodingType::CodePage:
                return L"codepage";
            default:
                return L"utf8";
            }
        }

        EncodingType EncodingFromKey(const std::wstring& key)
        {
            if (key == L"utf16le")
                return EncodingType::Utf16LE;
            if (key == L"utf16be")
                return EncodingType::Utf16BE;
            if (key == L"codepage")
                return EncodingType::CodePage;
            return EncodingType::Utf8;
        }

        std::wstring OptionsXml(const CompareOptions& options)
        {
            std::wstring out = L"  <options whitespace=\"" + std::to_wstring(static_cast<int>(options.whitespace)) + L"\" ignoreCase=\"" +
                std::to_wstring(options.ignoreCase ? 1 : 0) + L"\" ignoreEol=\"" + std::to_wstring(options.ignoreEol ? 1 : 0) + L"\" ignoreBlankLines=\"" +
                std::to_wstring(options.ignoreBlankLines ? 1 : 0) + L"\" absoluteTolerance=\"" + std::to_wstring(options.absoluteTolerance) +
                L"\" relativeTolerance=\"" + std::to_wstring(options.relativeTolerance) + L"\" pairing=\"" + XmlEscape(options.linePairingRule, true) + L"\">\n";
            for (const std::wstring& expression : options.lineExpressions)
                out += L"    <lineExpression>" + XmlEscape(expression) + L"</lineExpression>\n";
            for (const auto& block : options.blockExpressions)
                out += L"    <blockExpression begin=\"" + XmlEscape(block.first, true) + L"\" end=\"" + XmlEscape(block.second, true) + L"\"/>\n";
            return out + L"  </options>\n";
        }

        CompareOptions OptionsFrom(const XmlNode* node)
        {
            CompareOptions options;
            if (!node)
                return options;
            const int whitespace = node->IntAttribute(L"whitespace", 0);
            options.whitespace = whitespace == 1 ? WhitespaceMode::IgnoreChanges : (whitespace == 2 ? WhitespaceMode::IgnoreAll : WhitespaceMode::Compare);
            options.ignoreCase = node->IntAttribute(L"ignoreCase", 0) != 0;
            options.ignoreEol = node->IntAttribute(L"ignoreEol", 1) != 0;
            options.ignoreBlankLines = node->IntAttribute(L"ignoreBlankLines", 0) != 0;
            options.absoluteTolerance = _wtof(node->AttributeOr(L"absoluteTolerance", L"0").c_str());
            options.relativeTolerance = _wtof(node->AttributeOr(L"relativeTolerance", L"0").c_str());
            options.linePairingRule = node->AttributeOr(L"pairing", L"");
            for (const XmlNode& child : node->children)
            {
                if (child.name == L"lineExpression")
                    options.lineExpressions.push_back(child.text);
                else if (child.name == L"blockExpression")
                    options.blockExpressions.emplace_back(child.AttributeOr(L"begin", L""), child.AttributeOr(L"end", L""));
            }
            return options;
        }

        std::wstring Leaf(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }

        bool ParseArchive(const std::wstring& text, XmlNode& root, const wchar_t* kind, std::wstring& error)
        {
            if (!ParseXml(text, root, error))
                return false;
            if (root.name != L"mergefilesArchive" || root.AttributeOr(L"kind", L"") != kind)
            {
                error = L"This is not a MergeFiles archive of that kind.";
                return false;
            }
            if (root.IntAttribute(L"version", 0) > 1)
            {
                error = L"The archive was made by a newer version of MergeFiles.";
                return false;
            }
            return true;
        }
    }

    ArchiveKind ArchiveKindOf(const std::wstring& text)
    {
        // Only the start of the document is needed: <?xml ...?><mergefilesArchive version kind=...>
        const size_t start = text.find(L"<mergefilesArchive");
        if (start == std::wstring::npos || start > 200)
            return ArchiveKind::None;
        const size_t end = text.find(L'>', start);
        const std::wstring tag = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (tag.find(L"kind=\"files\"") != std::wstring::npos)
            return ArchiveKind::Files;
        if (tag.find(L"kind=\"folders\"") != std::wstring::npos)
            return ArchiveKind::Folders;
        return ArchiveKind::None;
    }

    std::wstring SaveComparisonArchive(const Comparison& comparison)
    {
        std::wstring out = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mergefilesArchive version=\"1\" kind=\"files\" panes=\"" + std::to_wstring(comparison.PaneCount()) + L"\">\n";
        out += OptionsXml(comparison.Options());
        for (int pane = 0; pane < comparison.PaneCount(); ++pane)
        {
            const TextBuffer& buffer = comparison.Buffer(pane);
            out += L"  <file pane=\"" + std::to_wstring(pane) + L"\" path=\"" + XmlEscape(buffer.path, true) + L"\" title=\"" + XmlEscape(buffer.title, true) +
                L"\" encoding=\"" + EncodingKey(buffer.encoding.type) + L"\" bom=\"" + std::to_wstring(buffer.encoding.bom ? 1 : 0) + L"\" codepage=\"" +
                std::to_wstring(buffer.encoding.codePage) + L"\" eol=\"" + std::to_wstring(static_cast<int>(buffer.eol)) + L"\">\n" +
                Base64Encode(EncodeText(buffer.GetAllText(), buffer.encoding)) + L"\n  </file>\n";
        }
        // Per-line state: bookmarks (with comments), edit markers, resolved marks.
        for (int pane = 0; pane < comparison.PaneCount(); ++pane)
        {
            const TextBuffer& buffer = comparison.Buffer(pane);
            for (int line = 0; line < buffer.LineCount(); ++line)
            {
                const Line& data = buffer.GetLine(line);
                if (data.flags == 0 && data.comment.empty())
                    continue;
                out += L"  <line pane=\"" + std::to_wstring(pane) + L"\" index=\"" + std::to_wstring(line) + L"\" flags=\"" + std::to_wstring(data.flags) + L"\">" +
                    XmlEscape(data.comment) + L"</line>\n";
            }
        }
        for (const SyncPoint& point : comparison.SyncPoints())
        {
            out += L"  <sync";
            for (int pane = 0; pane < comparison.PaneCount(); ++pane)
                out += L" line" + std::to_wstring(pane) + L"=\"" + std::to_wstring(point.line[pane]) + L"\"";
            out += L"/>\n";
        }
        return out + L"</mergefilesArchive>\n";
    }

    std::unique_ptr<Comparison> LoadComparisonArchive(const std::wstring& text, std::wstring& error)
    {
        XmlNode root;
        if (!ParseArchive(text, root, L"files", error))
            return nullptr;
        const int panes = root.IntAttribute(L"panes", 2) == 3 ? 3 : 2;
        auto comparison = std::make_unique<Comparison>(panes);
        comparison->SetOptions(OptionsFrom(root.Child(L"options")));
        for (const XmlNode& node : root.children)
        {
            if (node.name != L"file")
                continue;
            const int pane = node.IntAttribute(L"pane", -1);
            if (pane < 0 || pane >= panes)
                continue;
            std::string bytes;
            if (!Base64Decode(node.text, bytes))
            {
                error = L"The archive is damaged (file contents).";
                return nullptr;
            }
            TextEncoding encoding;
            encoding.type = EncodingFromKey(node.AttributeOr(L"encoding", L"utf8"));
            encoding.bom = node.IntAttribute(L"bom", 0) != 0;
            encoding.codePage = static_cast<unsigned>(node.IntAttribute(L"codepage", 0));
            const DecodedText decoded = DecodeText(bytes, &encoding);
            const std::wstring path = node.AttributeOr(L"path", L"");
            const std::wstring title = node.AttributeOr(L"title", Leaf(path));
            comparison->SetText(pane, decoded.text, (path.empty() ? title : Leaf(path)) + L" (archived)");
            TextBuffer& buffer = comparison->Buffer(pane);
            buffer.encoding = encoding;
            const int eol = node.IntAttribute(L"eol", static_cast<int>(Eol::CrLf));
            buffer.eol = eol >= 1 && eol <= 3 ? static_cast<Eol>(eol) : Eol::CrLf;
        }
        for (const XmlNode& node : root.children)
        {
            if (node.name == L"line")
                comparison->RestoreLineState(node.IntAttribute(L"pane", 0), node.IntAttribute(L"index", -1), static_cast<uint8_t>(node.IntAttribute(L"flags", 0)), node.text);
            else if (node.name == L"sync")
            {
                SyncPoint point;
                for (int pane = 0; pane < panes; ++pane)
                    point.line[pane] = node.IntAttribute(L"line" + std::to_wstring(pane), 0);
                comparison->AddSyncPoint(point);
            }
        }
        comparison->Recompare();
        return comparison;
    }

    std::wstring SaveFolderArchive(const FolderComparison& comparison)
    {
        const FolderOptions& options = comparison.Options();
        std::wstring out = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mergefilesArchive version=\"1\" kind=\"folders\" sides=\"" + std::to_wstring(comparison.SideCount()) + L"\">\n";
        for (int side = 0; side < comparison.SideCount(); ++side)
            out += L"  <root side=\"" + std::to_wstring(side) + L"\" path=\"" + XmlEscape(comparison.Root(side), true) + L"\"/>\n";
        out += L"  <folderOptions mode=\"" + std::to_wstring(static_cast<int>(options.mode)) + L"\" include=\"" + XmlEscape(options.include, true) + L"\" exclude=\"" +
            XmlEscape(options.exclude, true) + L"\"/>\n";
        out += OptionsXml(options.text);
        // Reachable items only, parents first; indices are renumbered.
        std::vector<int> order = { 0 }, newIndex(comparison.Items().size(), -1);
        for (size_t i = 0; i < order.size(); ++i)
        {
            for (int child : comparison.Item(order[i]).children)
                order.push_back(child);
        }
        for (size_t i = 0; i < order.size(); ++i)
            newIndex[static_cast<size_t>(order[i])] = static_cast<int>(i);
        for (int index : order)
        {
            const FolderItem& item = comparison.Item(index);
            out += L"  <item parent=\"" + std::to_wstring(item.parent >= 0 ? newIndex[static_cast<size_t>(item.parent)] : -1) + L"\" name=\"" + XmlEscape(item.name, true) +
                L"\" path=\"" + XmlEscape(item.relPath, true) + L"\" dir=\"" + std::to_wstring(item.isDir ? 1 : 0) + L"\" status=\"" +
                std::to_wstring(static_cast<int>(item.status)) + L"\" note=\"" + XmlEscape(item.note, true) + L"\">";
            for (int side = 0; side < comparison.SideCount(); ++side)
            {
                const FolderSide& info = item.side[side];
                out += L"<side exists=\"" + std::to_wstring(info.exists ? 1 : 0) + L"\" dir=\"" + std::to_wstring(info.isDir ? 1 : 0) + L"\" size=\"" + std::to_wstring(info.size) +
                    L"\" modified=\"" + std::to_wstring(info.modified) + L"\" readOnly=\"" + std::to_wstring(info.readOnly ? 1 : 0) + L"\"/>";
            }
            out += L"</item>\n";
        }
        return out + L"</mergefilesArchive>\n";
    }

    std::unique_ptr<FolderComparison> LoadFolderArchive(const std::wstring& text, std::wstring& error)
    {
        XmlNode root;
        if (!ParseArchive(text, root, L"folders", error))
            return nullptr;
        const int sides = root.IntAttribute(L"sides", 2) == 3 ? 3 : 2;
        std::vector<std::wstring> roots(static_cast<size_t>(sides));
        FolderOptions options;
        std::vector<FolderItem> items;
        for (const XmlNode& node : root.children)
        {
            if (node.name == L"root")
            {
                const int side = node.IntAttribute(L"side", -1);
                if (side >= 0 && side < sides)
                    roots[static_cast<size_t>(side)] = node.AttributeOr(L"path", L"");
            }
            else if (node.name == L"folderOptions")
            {
                const int mode = node.IntAttribute(L"mode", 1);
                options.mode = mode == 0 ? FolderCompareMode::Quick : (mode == 2 ? FolderCompareMode::Thorough : FolderCompareMode::Binary);
                options.include = node.AttributeOr(L"include", L"");
                options.exclude = node.AttributeOr(L"exclude", L"");
            }
            else if (node.name == L"options")
            {
                options.text = OptionsFrom(&node);
            }
            else if (node.name == L"item")
            {
                FolderItem item;
                item.parent = node.IntAttribute(L"parent", -1);
                item.name = node.AttributeOr(L"name", L"");
                item.relPath = node.AttributeOr(L"path", L"");
                item.isDir = node.IntAttribute(L"dir", 0) != 0;
                const int status = node.IntAttribute(L"status", 0);
                item.status = status >= 0 && status <= static_cast<int>(ItemStatus::Error) ? static_cast<ItemStatus>(status) : ItemStatus::Pending;
                item.note = node.AttributeOr(L"note", L"");
                int side = 0;
                for (const XmlNode& child : node.children)
                {
                    if (child.name != L"side" || side >= sides)
                        continue;
                    FolderSide& info = item.side[side++];
                    info.exists = child.IntAttribute(L"exists", 0) != 0;
                    info.isDir = child.IntAttribute(L"dir", 0) != 0;
                    info.size = wcstoull(child.AttributeOr(L"size", L"0").c_str(), nullptr, 10);
                    info.modified = wcstoll(child.AttributeOr(L"modified", L"0").c_str(), nullptr, 10);
                    info.readOnly = child.IntAttribute(L"readOnly", 0) != 0;
                }
                items.push_back(std::move(item));
            }
        }
        if (items.empty() || items[0].parent != -1)
        {
            error = L"The archive holds no folder comparison.";
            return nullptr;
        }
        auto comparison = std::make_unique<FolderComparison>(roots, options);
        comparison->RestoreItems(std::move(items));
        return comparison;
    }

    bool WriteArchiveFile(const std::wstring& path, const std::wstring& text, std::wstring& error)
    {
        return WriteFileBytes(path, EncodeText(text, { EncodingType::Utf8, false, 0 }), error);
    }

    bool ReadArchiveFile(const std::wstring& path, std::wstring& text, std::wstring& error)
    {
        std::string bytes;
        if (!ReadFileBytes(path, bytes, error))
            return false;
        const TextEncoding utf8 = { EncodingType::Utf8, false, 0 };
        text = DecodeText(bytes, &utf8).text;
        return true;
    }
}
