// Gives every local variable a numbered slot before the code first runs, so reading
// and writing it is an index into the function's frame instead of a search by name
// through nested scopes. Names that are not local are globals; code that was never
// resolved (the debugger's console) still looks names up by name.
#include "skrumi/Resolver.h"
#include "skrumi/AST.h"
#include <unordered_map>

namespace skrumi {
namespace {

class Resolver {
public:
    // A lambda's resolver has the resolver of the code around it as `parent`: a name
    // that is not the lambda's own is looked up there and becomes a capture.
    Resolver(FrameLayout& layout, Resolver* parent = nullptr, std::vector<Capture>* captures = nullptr)
        : layout_(layout), parent_(parent), captures_(captures) {}

    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }

    int declare(const std::string& name, const std::string& type, bool* duplicate = nullptr, bool constant = false) {
        auto& scope = scopes_.back();
        if (duplicate && scope.count(name)) *duplicate = true;
        int slot = static_cast<int>(layout_.names.size());
        layout_.names.push_back(name);
        layout_.boxed.push_back(false);
        layout_.types.push_back(type);
        layout_.constants.push_back(constant);
        scope[name] = slot;
        return slot;
    }

    VarRef find(const std::string& name) {
        VarRef ref;
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) {
                ref.slot = found->second;
                return ref;
            }
        }
        ref.slot = VarRef::global;
        if (!parent_) return ref;
        VarRef outer = parent_->find(name);
        if (outer.slot >= 0) {
            // The variable now lives in a box that the function and the lambda share.
            parent_->layout_.boxed[static_cast<size_t>(outer.slot)] = true;
            size_t slot = static_cast<size_t>(outer.slot);
            return capture(false, outer.slot, name, parent_->layout_.types[slot], parent_->layout_.constants[slot]);
        }
        if (outer.slot == VarRef::captured) {
            const Capture& from = (*parent_->captures_)[static_cast<size_t>(outer.capture)];
            return capture(true, outer.capture, name, from.type, from.constant);
        }
        return outer;
    }

    int size() const { return static_cast<int>(layout_.names.size()); }
    bool topLevel() const { return scopes_.empty(); }
    // With the program's top level in registers: the depth of its own scope.
    size_t programDepth = 0;
    bool atProgramLevel() const { return programDepth > 0 && scopes_.size() == programDepth; }

    void visit(Node* node);

private:
    FrameLayout& layout_;
    Resolver* parent_;
    std::vector<Capture>* captures_;
    std::vector<std::unordered_map<std::string, int>> scopes_;

    VarRef capture(bool fromCapture, int index, const std::string& name, const std::string& type, bool constant) {
        VarRef ref;
        ref.slot = VarRef::captured;
        for (size_t i = 0; i < captures_->size(); ++i) {
            const Capture& existing = (*captures_)[i];
            if (existing.fromCapture == fromCapture && existing.index == index) {
                ref.capture = static_cast<int>(i);
                return ref;
            }
        }
        captures_->push_back({fromCapture, index, name, type, constant});
        ref.capture = static_cast<int>(captures_->size()) - 1;
        return ref;
    }

    void block(BlockNode& block) {
        if (block.scoped) push();
        block.firstSlot = size();
        for (auto& stmt : block.stmts) visit(stmt.get());
        block.endSlot = size();
        if (block.scoped) pop();
    }

    void lambda(LambdaNode& node) {
        node.layout = std::make_shared<FrameLayout>();
        node.captures.clear();
        Resolver inner(*node.layout, this, &node.captures);
        inner.push();
        for (const auto& param : node.params) inner.declare(param.name, param.type);
        inner.visit(node.body.get());
        inner.pop();
    }
};

void Resolver::visit(Node* node) {
    if (!node) return;
    if (auto* n = dynamic_cast<BlockNode*>(node)) {
        block(*n);
    } else if (auto* n = dynamic_cast<VarAccessNode*>(node)) {
        n->ref = find(n->name);
    } else if (auto* n = dynamic_cast<VarAssignNode*>(node)) {
        visit(n->expr.get());
        n->ref = find(n->name);
    } else if (auto* n = dynamic_cast<PostIncNode*>(node)) {
        n->ref = find(n->name);
    } else if (auto* n = dynamic_cast<VarDeclNode*>(node)) {
        bool local = !n->global && !topLevel();
        if (local && n->kind == Value::Kind::Function && dynamic_cast<LambdaNode*>(n->expr.get())) {
            // func f = (n) => ... f(n - 1): a lambda may call itself by its variable.
            n->programGlobal = atProgramLevel();
            n->slot = declare(n->name, n->type, &n->duplicate, n->constant);
            visit(n->expr.get());
        } else {
            // The initializer sees the names before this one: `int x = x + 1;` reads an outer x.
            visit(n->expr.get());
            if (local) {
                n->programGlobal = atProgramLevel();
                n->slot = declare(n->name, n->type, &n->duplicate, n->constant);
            }
        }
    } else if (auto* n = dynamic_cast<ArrayDeclNode*>(node)) {
        visit(n->initializer.get());
        if (!n->global && !topLevel()) {
            n->programGlobal = atProgramLevel();
            n->slot = declare(n->name, n->type, &n->duplicate, n->constant);
        }
    } else if (auto* n = dynamic_cast<ForNode*>(node)) {
        push();
        n->firstSlot = size();
        visit(n->init.get());
        visit(n->condition.get());
        visit(n->step.get());
        visit(n->body.get());
        n->endSlot = size();
        pop();
    } else if (auto* n = dynamic_cast<ForInNode*>(node)) {
        visit(n->iterable.get());
        push();
        n->firstSlot = size();
        // The variables take consecutive slots: the loop fills them with one instruction.
        for (auto& variable : n->variables) variable.slot = declare(variable.name, variable.type);
        visit(n->body.get());
        n->endSlot = size();
        pop();
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        visit(n->condition.get());
        visit(n->body.get());
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        visit(n->condition.get());
        visit(n->thenB.get());
        visit(n->elseB.get());
    } else if (auto* n = dynamic_cast<SwitchNode*>(node)) {
        visit(n->expr.get());
        for (auto& item : n->cases) {
            visit(item.first.get());
            visit(item.second.get());
        }
        visit(n->defaultCase.get());
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        visit(n->body.get());
        if (n->handler) {
            push();
            if (!n->errorName.empty()) n->errorSlot = declare(n->errorName, "string");
            visit(n->handler.get());
            pop();
        }
        visit(n->cleanup.get());
    } else if (auto* n = dynamic_cast<FuncCallNode*>(node)) {
        // A local variable of this name holds the function to call.
        n->ref = find(n->name);
        for (auto& arg : n->args) visit(arg.get());
    } else if (auto* n = dynamic_cast<MethodCallNode*>(node)) {
        visit(n->base.get());
        for (auto& arg : n->args) visit(arg.get());
    } else if (auto* n = dynamic_cast<CallNode*>(node)) {
        visit(n->callee.get());
        for (auto& arg : n->args) visit(arg.get());
    } else if (auto* n = dynamic_cast<LambdaNode*>(node)) {
        lambda(*n);
    } else if (auto* n = dynamic_cast<ReturnNode*>(node)) {
        visit(n->expr.get());
    } else if (auto* n = dynamic_cast<BinOpNode*>(node)) {
        visit(n->left.get());
        visit(n->right.get());
    } else if (auto* n = dynamic_cast<UnaryOpNode*>(node)) {
        visit(n->operand.get());
    } else if (auto* n = dynamic_cast<ArrayLiteralNode*>(node)) {
        for (auto& element : n->elements) visit(element.get());
    } else if (auto* n = dynamic_cast<IndexNode*>(node)) {
        visit(n->base.get());
        visit(n->index.get());
    } else if (auto* n = dynamic_cast<FieldNode*>(node)) {
        visit(n->base.get());
    } else if (auto* n = dynamic_cast<SetNode*>(node)) {
        visit(n->value.get());
        visit(n->target.get());
    } else if (auto* n = dynamic_cast<MapLiteralNode*>(node)) {
        for (auto& entry : n->entries) {
            visit(entry.first.get());
            visit(entry.second.get());
        }
    } else if (auto* n = dynamic_cast<InterpolationNode*>(node)) {
        for (auto& part : n->parts) visit(part.get());
    } else if (auto* n = dynamic_cast<ThrowNode*>(node)) {
        visit(n->message.get());
    }
    // A function defined inside is resolved when it is first called; struct defaults,
    // literals, using and include have no names of their own to resolve.
}

} // namespace

void resolveFunction(FuncDefNode& function) {
    auto* body = dynamic_cast<BlockNode*>(function.body.get());
    if (!body || body->layout) return;
    auto layout = std::make_shared<FrameLayout>();
    Resolver resolver(*layout);
    resolver.push();
    for (const auto& param : function.params) resolver.declare(param.name, param.type);
    resolver.visit(body);
    resolver.pop();
    body->layout = layout;
}

void resolveProgram(BlockNode& program, bool registers) {
    if (program.layout) return;
    auto layout = std::make_shared<FrameLayout>();
    Resolver resolver(*layout);
    if (!registers) {
        // The top level declares globals; only blocks inside it get slots.
        resolver.visit(&program);
    } else {
        resolver.push();
        resolver.programDepth = 1;
        program.firstSlot = resolver.size();
        for (auto& stmt : program.stmts) resolver.visit(stmt.get());
        program.endSlot = resolver.size();
        resolver.pop();
    }
    program.layout = layout;
}

void resolveExpression(Node& expression) {
    FrameLayout layout;
    Resolver resolver(layout);
    resolver.visit(&expression);
}

} // namespace skrumi
