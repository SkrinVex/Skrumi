#include "DocumentManager.h"
#include "skrumi/Lexer.h"
#include "skrumi/Parser.h"
#include "skrumi/Runtime.h"
#include <algorithm>
#include <cctype>

namespace skrumi {
namespace lsp {

DocumentManager::DocumentManager(std::string home) : skrumiHome(std::move(home)) {
    sources = std::make_shared<OverlaySources>(filesystemSources(skrumiHome));
    project = std::make_unique<ProjectIndex>(sources);
}

std::string DocumentManager::uriToFilePath(const std::string& uri) {
    if (uri.compare(0, 7, "file://") != 0) return uri;
    std::string encoded = uri.substr(7);
    // file://host/share is a UNC path; file:///path has an empty host.
    if (!encoded.empty() && encoded[0] != '/') encoded = "//" + encoded;
    std::string path;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size() &&
            std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) && std::isxdigit(static_cast<unsigned char>(encoded[i + 2]))) {
            path += static_cast<char>(std::stoi(encoded.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            path += encoded[i];
        }
    }
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1); // /C:/dir -> C:/dir
#endif
    return path;
}

std::string DocumentManager::filePathToUri(const std::string& path) {
    if (path.compare(0, 7, "file://") == 0) return path;
    std::string normalized = path;
    for (auto& ch : normalized) if (ch == '\\') ch = '/';
    if (!normalized.empty() && normalized[0] != '/') normalized = "/" + normalized; // C:/dir -> /C:/dir
    static const char* hex = "0123456789ABCDEF";
    std::string uri = "file://";
    for (unsigned char ch : normalized) {
        if (std::isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == ':') {
            uri += static_cast<char>(ch);
        } else {
            uri += '%';
            uri += hex[ch >> 4];
            uri += hex[ch & 15];
        }
    }
    return uri;
}

void DocumentManager::setWorkspaceRoot(const std::string& uri) {
    project->setRoot(uri.empty() ? "" : uriToFilePath(uri));
}

std::vector<std::string> DocumentManager::openDocument(const std::string& uri, const std::string& text, int version) {
    if (!isSourceFile(uriToFilePath(uri))) return {};
    DocumentState& doc = documents[uri];
    doc.uri = uri;
    doc.text = text;
    doc.version = version;
    sources->set(uriToFilePath(uri), text);
    std::vector<std::string> before = doc.peers;
    analyze(doc);
    // A file that joined or left the program must be analyzed again as well.
    before.insert(before.end(), doc.peers.begin(), doc.peers.end());
    return reanalyzePeers(uri, before);
}

std::vector<std::string> DocumentManager::updateDocument(const std::string& uri, const std::string& text, int version) {
    return openDocument(uri, text, version);
}

std::vector<std::string> DocumentManager::closeDocument(const std::string& uri) {
    std::vector<std::string> peers;
    auto found = documents.find(uri);
    if (found != documents.end()) peers = found->second.peers;
    documents.erase(uri);
    sources->erase(uriToFilePath(uri));
    return reanalyzePeers(uri, peers);
}

std::vector<std::string> DocumentManager::refresh() {
    std::vector<std::string> changed;
    for (auto& entry : documents) {
        analyze(entry.second);
        changed.push_back(entry.first);
    }
    return changed;
}

// A change in one file can define or remove what the other files of its program use.
// Peers are symmetric: a file's peers have it among their own peers.
std::vector<std::string> DocumentManager::reanalyzePeers(const std::string& uri, std::vector<std::string> peers) {
    std::vector<std::string> changed;
    if (documents.count(uri)) changed.push_back(uri);
    std::sort(peers.begin(), peers.end());
    for (auto& entry : documents) {
        if (entry.first == uri) continue;
        std::string path = canonicalPath(uriToFilePath(entry.first));
        if (!std::binary_search(peers.begin(), peers.end(), path)) continue;
        analyze(entry.second);
        changed.push_back(entry.first);
    }
    return changed;
}

const DocumentState* DocumentManager::getDocument(const std::string& uri) const {
    auto it = documents.find(uri);
    return it != documents.end() ? &it->second : nullptr;
}

std::vector<LspDiagnostic> DocumentManager::getDiagnostics(const std::string& uri) const {
    const DocumentState* doc = getDocument(uri);
    return doc ? doc->diagnostics : std::vector<LspDiagnostic>{};
}

void DocumentManager::forEachProgramFile(const std::string& uri,
                                         const std::function<void(const std::string&, const SemanticAnalyzer&)>& visit) {
    const DocumentState* doc = getDocument(uri);
    if (!doc || !doc->analyzer) return;
    visit(uri, *doc->analyzer);
    for (const auto& peer : doc->peers) {
        std::string peerUri = filePathToUri(peer);
        bool open = false;
        for (const auto& entry : documents) {
            if (entry.first == uri || canonicalPath(uriToFilePath(entry.first)) != peer) continue;
            if (entry.second.analyzer) visit(entry.first, *entry.second.analyzer);
            open = true;
        }
        if (open) continue;
        // A file nobody has open is read from disk (through the overlay, so open
        // buffers it includes are current) and analyzed with its own program.
        DocumentState state;
        state.uri = peerUri;
        try {
            state.text = sources->read(peer);
        } catch (const std::exception&) {
            continue;
        }
        analyze(state);
        if (state.analyzer) visit(peerUri, *state.analyzer);
    }
}

void DocumentManager::analyze(DocumentState& doc) {
    std::string filePath = uriToFilePath(doc.uri);

    Lexer lexer(doc.text, true);
    auto tokens = lexer.tokenize();
    std::vector<Diagnostic> allDiags = lexer.getDiagnostics();
    for (auto& d : allDiags) if (d.code == 0) d.code = 1000;

    Parser parser(std::move(tokens), canonicalPath(filePath));
    std::vector<Diagnostic> parserDiags;
    auto program = parser.parseProgramWithDiagnostics(parserDiags);
    for (auto& d : parserDiags) if (d.code == 0) d.code = 1001;
    allDiags.insert(allDiags.end(), parserDiags.begin(), parserDiags.end());

    // Semantic analysis never executes code.
    doc.analyzer = std::make_unique<SemanticAnalyzer>(canonicalPath(filePath), skrumiHome, sources);
    doc.peers.clear();
    if (filePath.compare(0, 1, "/") == 0 || (filePath.size() > 2 && filePath[1] == ':')) {
        doc.peers = project->peers(filePath);
        doc.analyzer->addProjectFiles(doc.peers);
    }
    doc.analyzer->analyze(program.get());
    const auto& semDiags = doc.analyzer->getDiagnostics();
    // Keep the partial symbol index for completion, but do not cascade semantic
    // errors from declarations that the parser could not reconstruct.
    if (allDiags.empty()) allDiags.insert(allDiags.end(), semDiags.begin(), semDiags.end());

    doc.diagnostics.clear();
    for (const auto& d : allDiags) {
        if (doc.diagnostics.size() == 10) break;
        doc.diagnostics.push_back(LspDiagnostic::fromDiagnostic(d));
    }
}

} // namespace lsp
} // namespace skrumi
