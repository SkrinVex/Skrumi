#include "skrumi/AST.h"
#include "skrumi/Bytecode.h"
#include <cerrno>
#include <cstdlib>

namespace skrumi {

void CallDepth::exceeded(const std::string& name, int depth) {
    throw std::runtime_error("Runtime Error: call depth limit reached in '" + name + "' after " + std::to_string(depth) +
                             " nested calls (recursion without a base case?)");
}

FuncDefNode::FuncDefNode(std::string rt, std::string n, std::vector<FuncParam> p, std::shared_ptr<Node> b, SourceRange nr)
    : returnType(std::move(rt)), name(std::move(n)), params(std::move(p)), body(std::move(b)), nameRange(nr) {
    block_ = dynamic_cast<BlockNode*>(body.get());
}

void FuncDefNode::declare(Context& root) {
    root.getRoot()->defineFunc(name, std::make_shared<FuncDefNode>(returnType, name, params, body, nameRange));
}

Value FuncDefNode::invoke(ValueList args, Context& caller) const {
    return vm::call(*this, args.data(), args.size(), caller);
}

NumberNode::NumberNode(std::string v) : val(std::move(v)) {
    std::string normalized;
    for (char c : val) if (c != '_') normalized += c;
    size_t offset = !normalized.empty() && normalized[0] == '-' ? 1 : 0;
    bool hex = normalized.compare(offset, 2, "0x") == 0 || normalized.compare(offset, 2, "0X") == 0;
    bool binary = normalized.compare(offset, 2, "0b") == 0 || normalized.compare(offset, 2, "0B") == 0;
    isFloat = !hex && !binary && normalized.find_first_of(".eE") != std::string::npos;
    if (isFloat) {
        literal = Value::real(std::strtod(normalized.c_str(), nullptr));
    } else {
        errno = 0;
        if (hex || binary) normalized.erase(offset, 2);
        long long parsed = std::strtoll(normalized.c_str(), nullptr, hex ? 16 : binary ? 2 : 10);
        // Digits that do not fit even 64 bits are an error wherever the literal is used.
        tooBig = errno == ERANGE;
        literal = Value::integer(parsed);
    }
}

void StructDefNode::declare(Context& root) {
    root.getRoot()->structs[type->name] = type;
}

void EnumDefNode::declare(Context& context) {
    Context& root = *context.getRoot();
    auto existing = root.structs.find(type->name);
    if (existing != root.structs.end() && existing->second == type && root.variables.count(type->name)) return;
    root.structs[type->name] = type;
    // The name of the enum is a constant map of its values: Color.Red, for (name, c in Color).
    Value values = runtime::makeMap();
    for (const auto& member : members) {
        Value item = Value::container(Value::Kind::Struct);
        Object& object = *item.ref();
        object.structType = type;
        object.items = {Value::string(member.name), member.value};
        object.frozen = true;
        values.ref()->slot(member.name) = std::move(item);
    }
    values.ref()->frozen = true;
    root.variables[type->name] = std::move(values);
    root.constants.insert(type->name);
    ++root.generation;
}

} // namespace skrumi
