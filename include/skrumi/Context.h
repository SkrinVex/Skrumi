#pragma once
#include <string>
#include <random>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <iosfwd>
#include <stdexcept>
#include <utility>
#include "skrumi/SourceLocation.h"

namespace skrumi {

struct Node;
class Interpreter;
namespace bytecode { struct Proto; }
namespace runtime { struct Builtin; }
namespace graphics { class Window; class Fonts; class Canvases; }
namespace audio { class Mixer; }
namespace platform { struct ServerState; struct ProcessTable; struct FileLocks; }

struct FuncParam {
    std::string type;
    std::string name;
    SourceRange range; // where a parameter of a Skrumi function is named; empty for builtins

    FuncParam() = default;
    FuncParam(std::string t, std::string n, SourceRange r = {}) : type(std::move(t)), name(std::move(n)), range(r) {}
};

struct Object;

// The text of a string value. It never changes once made, so every copy of the value
// shares it; the count is not atomic because values live on the interpreter thread.
struct StringData {
    unsigned refs = 1;
    std::string text;
    explicit StringData(std::string t) : text(std::move(t)) {}
    // Strings come and go by the million: their cells are reused from a free list.
    static void* operator new(std::size_t size);
    static void operator delete(void* memory, std::size_t size) noexcept;
};

// A value is its kind and one machine word: an int, a float or a bool are stored in
// it, a string and a container (array, map, struct) are pointers to shared data that
// the value counts references to. Copying a value never copies text or elements.
class Value {
public:
    // Function: a Skrumi function, a builtin or a lambda as a value. Box: the cell of a
    // local variable that a lambda captured; code reads through it, a program never
    // sees one.
    enum class Kind : unsigned char { Void, Int, Float, Bool, String, Array, Map, Struct, Function, Box };

    Value() noexcept { data_.bits = 0; }
    Value(const Value& other) noexcept : kind_(other.kind_), data_(other.data_) { retain(); }
    Value(Value&& other) noexcept : kind_(other.kind_), data_(other.data_) { other.kind_ = Kind::Void; }
    Value& operator=(const Value& other) noexcept {
        // Numbers, bools and void are copied in place; the rest count references.
        if (kind_ < Kind::String && other.kind_ < Kind::String) {
            kind_ = other.kind_;
            data_ = other.data_;
            return *this;
        }
        return assignShared(other);
    }
    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            release();
            kind_ = other.kind_;
            data_ = other.data_;
            other.kind_ = Kind::Void;
        }
        return *this;
    }
    ~Value() { release(); }
    void swap(Value& other) noexcept {
        std::swap(kind_, other.kind_);
        std::swap(data_, other.data_);
    }

    static Value integer(long long value) noexcept {
        Value v;
        v.kind_ = Kind::Int;
        v.data_.integer = value;
        return v;
    }
    static Value real(double value) noexcept {
        Value v;
        v.kind_ = Kind::Float;
        v.data_.real = value;
        return v;
    }
    static Value boolean(bool value) noexcept {
        Value v;
        v.kind_ = Kind::Bool;
        v.data_.boolean = value;
        return v;
    }
    static Value string(std::string text) {
        Value v;
        v.data_.string = new StringData(std::move(text));
        v.kind_ = Kind::String;
        return v;
    }
    // A new, empty array, map or struct.
    static Value container(Kind kind);

    // Empty again (void), letting go of what it held.
    void reset() noexcept {
        release();
        kind_ = Kind::Void;
    }
    // Overwrite with a scalar in place, for the VM's arithmetic.
    void setInt(long long value) noexcept {
        release();
        kind_ = Kind::Int;
        data_.integer = value;
    }
    void setReal(double value) noexcept {
        release();
        kind_ = Kind::Float;
        data_.real = value;
    }
    void setBool(bool value) noexcept {
        release();
        kind_ = Kind::Bool;
        data_.boolean = value;
    }

    Kind kind() const { return kind_; }
    bool is(Kind kind) const { return kind_ == kind; }
    bool isVoid() const { return kind_ == Kind::Void; }
    bool isInt() const { return kind_ == Kind::Int; }
    bool isFloat() const { return kind_ == Kind::Float; }
    bool isNumber() const { return kind_ == Kind::Int || kind_ == Kind::Float; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isString() const { return kind_ == Kind::String; }
    bool isContainer() const { return kind_ >= Kind::Array; }
    bool isFunction() const { return kind_ == Kind::Function; }

    // The payload of a value known to be of that kind.
    long long asInt() const { return data_.integer; }
    double asFloat() const { return data_.real; }
    bool asBool() const { return data_.boolean; }
    const std::string& str() const { return data_.string->text; }
    // An array, a map, a struct, a function or a box; null for everything else.
    Object* ref() const { return isContainer() ? data_.object : nullptr; }

    // A scalar as text: 42, 2.5, true, the string itself; empty for void and containers.
    std::string text() const;
    // "void", "int", "float", "bool", "string", "array", "map" or the struct's name.
    const std::string& typeName() const;
    static const std::string& nameOf(Kind kind);

private:
    Kind kind_ = Kind::Void;
    union Data {
        long long integer;
        double real;
        bool boolean;
        StringData* string;
        Object* object;
        unsigned long long bits;
    } data_;

    void retain() const noexcept {
        if (kind_ >= Kind::String) retainShared();
    }
    void release() noexcept {
        if (kind_ >= Kind::String) releaseShared();
    }
    void retainShared() const noexcept;
    void releaseShared() noexcept;
    Value& assignShared(const Value& other) noexcept;
    static void freeString(StringData* string) noexcept;
    static void freeObject(Object* object) noexcept;
    friend struct Object;
};

std::ostream& operator<<(std::ostream& out, const Value& value);

// The arguments of a call: consecutive values that the caller keeps alive, such as a
// vector or the registers of the bytecode VM. Builtins read them without copying.
// The elements of an array (and the fields of a struct): a vector of values that grows
// with realloc. A value holds no pointer to itself, so moving the block moves the
// values; the allocator can then extend a big block in place instead of copying it.
class ValueList {
public:
    using value_type = Value;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using reference = Value&;
    using const_reference = const Value&;
    using pointer = Value*;
    using const_pointer = const Value*;
    using iterator = Value*;
    using const_iterator = const Value*;

    ValueList() noexcept = default;
    explicit ValueList(size_t count) { resize(count); }
    ValueList(size_t count, const Value& value) { assign(count, value); }
    ValueList(std::initializer_list<Value> values) { assign(values.begin(), values.end()); }
    template <class It, class = decltype(*std::declval<It&>(), ++std::declval<It&>())>
    ValueList(It first, It last) { assign(first, last); }
    ValueList(const ValueList& other) { assign(other.begin(), other.end()); }
    ValueList(ValueList&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }
    ~ValueList() { destroy(); }
    ValueList& operator=(const ValueList& other) {
        if (this != &other) {
            ValueList copy(other);
            swap(copy);
        }
        return *this;
    }
    ValueList& operator=(ValueList&& other) noexcept {
        if (this != &other) {
            ValueList gone(std::move(*this));
            swap(other);
        }
        return *this;
    }

    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return size_ == 0; }
    Value* data() noexcept { return data_; }
    const Value* data() const noexcept { return data_; }
    Value* begin() noexcept { return data_; }
    Value* end() noexcept { return data_ + size_; }
    const Value* begin() const noexcept { return data_; }
    const Value* end() const noexcept { return data_ + size_; }
    const Value* cbegin() const noexcept { return data_; }
    const Value* cend() const noexcept { return data_ + size_; }
    Value& operator[](size_t index) noexcept { return data_[index]; }
    const Value& operator[](size_t index) const noexcept { return data_[index]; }
    Value& front() noexcept { return data_[0]; }
    const Value& front() const noexcept { return data_[0]; }
    Value& back() noexcept { return data_[size_ - 1]; }
    const Value& back() const noexcept { return data_[size_ - 1]; }

    void reserve(size_t count) {
        if (count > capacity_) reallocate(count);
    }
    void push_back(const Value& value) {
        if (size_ == capacity_) {
            Value copy(value); // the value may be one of the elements
            grow();
            new (data_ + size_) Value(std::move(copy));
        } else {
            new (data_ + size_) Value(value);
        }
        ++size_;
    }
    void push_back(Value&& value) {
        if (size_ == capacity_) {
            Value moved(std::move(value));
            grow();
            new (data_ + size_) Value(std::move(moved));
        } else {
            new (data_ + size_) Value(std::move(value));
        }
        ++size_;
    }
    template <class... Args>
    Value& emplace_back(Args&&... args) {
        Value value(std::forward<Args>(args)...);
        push_back(std::move(value));
        return back();
    }
    void pop_back() noexcept { data_[--size_].~Value(); }
    void clear() noexcept {
        size_t count = size_;
        size_ = 0; // a destructor may look at this list again
        for (size_t i = count; i > 0; --i) data_[i - 1].~Value();
    }
    void resize(size_t count) { resize(count, Value()); }
    void resize(size_t count, const Value& value) {
        if (count < size_) {
            while (size_ > count) pop_back();
            return;
        }
        Value fill(value);
        reserve(count);
        while (size_ < count) new (data_ + size_++) Value(fill);
    }
    void assign(size_t count, const Value& value) {
        clear();
        resize(count, value);
    }
    template <class It>
    void assign(It first, It last) {
        ValueList fresh;
        for (; first != last; ++first) fresh.push_back(*first);
        swap(fresh);
    }
    Value* insert(const Value* at, Value value) {
        size_t index = static_cast<size_t>(at - data_);
        if (size_ == capacity_) grow();
        std::memmove(static_cast<void*>(data_ + index + 1), static_cast<const void*>(data_ + index),
                     (size_ - index) * sizeof(Value));
        new (data_ + index) Value(std::move(value));
        ++size_;
        return data_ + index;
    }
    template <class It, class = decltype(*std::declval<It&>(), ++std::declval<It&>())>
    Value* insert(const Value* at, It first, It last) {
        size_t index = static_cast<size_t>(at - data_);
        ValueList added(first, last);
        size_t count = added.size_;
        if (count == 0) return data_ + index;
        reserve(size_ + count > capacity_ * 2 ? size_ + count : capacity_ * 2);
        std::memmove(static_cast<void*>(data_ + index + count), static_cast<const void*>(data_ + index),
                     (size_ - index) * sizeof(Value));
        std::memcpy(static_cast<void*>(data_ + index), static_cast<const void*>(added.data_), count * sizeof(Value));
        added.size_ = 0; // the values moved over
        size_ += count;
        return data_ + index;
    }
    Value* erase(const Value* at) { return erase(at, at + 1); }
    Value* erase(const Value* first, const Value* last) {
        size_t index = static_cast<size_t>(first - data_);
        size_t count = static_cast<size_t>(last - first);
        if (count == 0) return data_ + index;
        ValueList gone;
        gone.reserve(count);
        std::memcpy(static_cast<void*>(gone.data_), static_cast<const void*>(data_ + index), count * sizeof(Value));
        gone.size_ = count;
        std::memmove(static_cast<void*>(data_ + index), static_cast<const void*>(data_ + index + count),
                     (size_ - index - count) * sizeof(Value));
        size_ -= count;
        return data_ + index; // the erased values go when `gone` does, after the list is whole
    }
    void swap(ValueList& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }
    friend bool operator==(const ValueList& a, const ValueList& b) = delete;

private:
    Value* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;

    void grow() { reallocate(capacity_ < 4 ? 4 : capacity_ * 2); }
    void reallocate(size_t count) {
        void* block = std::realloc(static_cast<void*>(data_), count * sizeof(Value));
        if (!block) throw std::bad_alloc();
        data_ = static_cast<Value*>(block);
        capacity_ = count;
    }
    void destroy() noexcept {
        clear();
        std::free(static_cast<void*>(data_));
        data_ = nullptr;
        capacity_ = 0;
    }
};

class Arguments {
public:
    Arguments(const Value* data, size_t count) : data_(data), count_(count) {}
    Arguments(const ValueList& values) : data_(values.data()), count_(values.size()) {}
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const Value& operator[](size_t index) const { return data_[index]; }
    const Value* begin() const { return data_; }
    const Value* end() const { return data_ + count_; }

private:
    const Value* data_;
    size_t count_;
};

struct StructType {
    std::string name;
    std::vector<FuncParam> fields;
    std::vector<std::shared_ptr<Node>> defaults; // an initial value per field, or null
    bool isEnum = false; // an enum: fields name and value; its values are fixed objects
    // Methods by name: Skrumi functions (FuncDefNode) whose first parameter is `this`.
    std::unordered_map<std::string, std::shared_ptr<const Node>> methods;
    // What each field's type holds, worked out when the first value is built, and the
    // defaults' compiled code.
    mutable std::vector<Value::Kind> kinds;
    mutable std::vector<std::shared_ptr<bytecode::Proto>> defaultCode;
};

// What a function value calls: a Skrumi function, a builtin, or a lambda's code, whose
// captured variables are the boxes in Object::items.
struct Callee {
    std::string name;                     // for messages and print(): "add", "lambda"
    std::shared_ptr<const Node> function; // a Skrumi function (a FuncDefNode)
    const runtime::Builtin* builtin = nullptr;
    std::shared_ptr<bytecode::Proto> lambda;
};

struct Object {
    enum class Kind { Array, Map, Struct, Function, Box };
    explicit Object(Kind k) : kind(k) {}
    ~Object(); // leaves the list of live containers
    // Containers come and go by the million: their memory is reused from a free list.
    static void* operator new(std::size_t size);
    static void operator delete(void* memory, std::size_t size) noexcept;
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Kind kind;
    unsigned refs = 0; // the values naming this container
    // Every live container is in one table, for the cycle collector (collectCycles).
    // The fields are packed so that a container stays in the same allocation size.
    int gcRefs = 0;
    unsigned gcRound = 0; // the collection that last set gcRefs
    std::uint32_t gcIndex = 0;
    bool frozen = false; // an enum's values and its map of them cannot be changed
    bool moduleAlias = false; // using math as m: m.name(...) reaches builtins too
    // array<int>, map<string, int>: the type every element (map value) has, interned;
    // null for a container that takes any values.
    const std::string* elementType = nullptr;
    // Array: the elements. Struct: the fields in declaration order.
    ValueList items;
    // Map: keys in insertion order, items[i] belongs to keys[i].
    std::vector<std::string> keys;
    std::shared_ptr<const StructType> structType;
    std::shared_ptr<const Callee> callee; // for a function

    // Map access; -1 when the key is absent.
    long find(const std::string& key) const;
    Value& slot(const std::string& key); // inserts a void value for a new key
    Value& slot(std::string&& key);
    bool erase(const std::string& key);
    // Drops everything the container holds: how the cycle collector breaks a ring.
    void clearContents();
    // A big map's index of its keys (defined in Context.cpp).
    struct KeyIndex;
    struct KeyIndexDeleter { void operator()(KeyIndex* index) const noexcept; };

private:
    // Where each key sits in `keys`, built once the map is big enough for a scan to
    // cost more than hashing; it is in step whenever it has a hash for every key.
    // Only big maps pay for it: every array, struct and small map keeps a null pointer.
    mutable std::unique_ptr<KeyIndex, KeyIndexDeleter> index_;
    KeyIndex& indexed() const;
    Value& added(std::string&& key); // a new key, at the end
};

inline void Value::retainShared() const noexcept {
    if (kind_ == Kind::String) ++data_.string->refs;
    else ++data_.object->refs;
}

inline void Value::releaseShared() noexcept {
    // Only counting here, small enough to inline everywhere; freeing is out of line.
    if (kind_ == Kind::String) {
        if (--data_.string->refs == 0) freeString(data_.string);
    } else if (--data_.object->refs == 0) {
        freeObject(data_.object);
    }
    kind_ = Kind::Void;
}

inline Value& Value::assignShared(const Value& other) noexcept {
    other.retain(); // first: assigning a value to itself must not free it
    release();
    kind_ = other.kind_;
    data_ = other.data_;
    return *this;
}

// exit(code) unwinds the whole program; it is deliberately not a std::exception,
// so handlers that report runtime errors never swallow it.
struct ExitRequest {
    int code = 0;
};

struct Context {
    Context* parent = nullptr;
    Interpreter* interpreter = nullptr;
    std::map<std::string, Value> variables;
    // Globals declared with a type T?: an assignment converts to it and may store null.
    std::unordered_map<std::string, std::string> nullableGlobals;
    std::set<std::string> constants; // globals declared const
    // While the program runs, the variables of its top level are its registers; code
    // that names them (a function, a module, the debugger) finds them here.
    std::unordered_map<std::string, Value*> programGlobals;
    std::map<std::string, std::shared_ptr<Node>> functions;
    // Functions replaced by a definition with another body: their code may still be
    // running, so it is kept until the functions are cleared.
    std::vector<std::shared_ptr<Node>> retired;
    std::map<std::string, std::shared_ptr<const StructType>> structs;
    // The numbered variables of the running function (or of the program's blocks),
    // shared by every block scope inside it; the frame owner is the scope that made them.
    Value* slots = nullptr;
    const std::vector<std::string>* slotNames = nullptr;
    bool frame = false;
    // Under the debugger, the frame owner marks the slots whose declaration has run, so
    // a variable holding null is told apart from one not declared yet.
    std::vector<char> declared;
    // The root counts how often its variables were cleared and its functions changed,
    // so what calls and globals cached goes stale.
    unsigned generation = 0;
    unsigned functionGeneration = 0;
    std::shared_ptr<graphics::Window> graphics;
    std::shared_ptr<graphics::Fonts> fonts;
    std::shared_ptr<graphics::Canvases> canvases;
    std::shared_ptr<audio::Mixer> mixer;
    std::shared_ptr<std::mt19937> randomEngine;
    std::shared_ptr<platform::ServerState> server;
    std::shared_ptr<platform::ProcessTable> processes;
    std::shared_ptr<platform::FileLocks> fileLocks;

    Context() = default;
    // A block scope inside `outer`: same function frame, own names.
    explicit Context(Context& outer)
        : parent(&outer), interpreter(outer.interpreter), slots(outer.slots), slotNames(outer.slotNames) {}
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Value getVar(const std::string& name) const;
    // The variable itself, for assignment through it; null when it is not declared.
    Value* findVar(const std::string& name);
    Context* getRoot() {
        Context* root = this;
        while (root->parent) root = root->parent;
        return root;
    }
    const Context* getRoot() const {
        const Context* root = this;
        while (root->parent) root = root->parent;
        return root;
    }

    void defineFunc(const std::string& name, std::shared_ptr<Node> func);
    std::shared_ptr<Node> getFunc(const std::string& name) const;
    std::shared_ptr<const StructType> getStruct(const std::string& name) const;

    void defineVar(const std::string& name, const std::string& type, const Value& value);
    void setVar(const std::string& name, Value val);

    // The elements of an array value, for builtins and the debugger.
    ValueList& arrayOf(const Value& value, const std::string& what);
};

namespace runtime {
Value makeArray(ValueList items);
// Frees containers that only hold each other (a[0] = a, two structs naming each
// other) and that nothing else reaches. Runs by itself as containers are created;
// returns how many containers it freed.
size_t collectCycles();
Value makeMap();
// A new container with the same contents, nested containers copied too (copy()).
Value deepCopy(const Value& value);
// Stores a value into an existing variable, converting it to the variable's type.
void assign(Value& target, Value val, const std::string& name);
bool deepEqual(const Value& a, const Value& b);
} // namespace runtime

} // namespace skrumi
