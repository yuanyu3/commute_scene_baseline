/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * Host-only stub implementing jiuwen::AnyValue symbols used by sa_agent config factory tests.
 * Not used when linking against the real libjiuwen-lite.so on device.
 */
#include "AnyValue.h"
#include "Tool.h"

namespace jiuwen {

AnyValue::AnyValue() : value_(nullptr) {}

AnyValue::~AnyValue() = default;

AnyValue::AnyValue(const AnyValue& other) : value_(other.value_) {}

AnyValue& AnyValue::operator=(const AnyValue& other)
{
    if (this != &other) {
        value_ = other.value_;
    }
    return *this;
}

AnyValue::AnyValue(AnyValue&& other) : value_(std::move(other.value_)) {}

AnyValue& AnyValue::operator=(AnyValue&& other)
{
    if (this != &other) {
        value_ = std::move(other.value_);
    }
    return *this;
}

AnyValue::AnyValue(std::nullptr_t) : value_(nullptr) {}

AnyValue::AnyValue(bool v) : value_(v) {}

AnyValue::AnyValue(char v) : value_(v) {}

AnyValue::AnyValue(unsigned char v) : value_(v) {}

AnyValue::AnyValue(wchar_t v) : value_(v) {}

AnyValue::AnyValue(int32_t v) : value_(v) {}

AnyValue::AnyValue(uint32_t v) : value_(v) {}

AnyValue::AnyValue(int64_t v) : value_(v) {}

AnyValue::AnyValue(uint64_t v) : value_(v) {}

AnyValue::AnyValue(float v) : value_(v) {}

AnyValue::AnyValue(double v) : value_(v) {}

AnyValue::AnyValue(const char* s) : value_(s == nullptr ? std::string() : std::string(s)) {}

AnyValue::AnyValue(const std::string& s) : value_(s) {}

AnyValue::AnyValue(std::string&& s) : value_(std::move(s)) {}

AnyValue::AnyValue(JsonString js) : value_(std::move(js)) {}

AnyValue::AnyValue(VectorAnyValue vector) : value_(std::move(vector)) {}

AnyValue::AnyValue(StringUnorderedMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(std::unordered_map<std::string, std::unordered_map<std::string, jiuwen::AnyValue>> map)
    : value_(std::move(map))
{
}

AnyValue::AnyValue(std::vector<std::shared_ptr<jiuwen::BaseMessage>> baseMessages) : value_(std::move(baseMessages)) {}

AnyValue::AnyValue(VectorToolInfo tools) : value_(std::move(tools)) {}

AnyValue::AnyValue(StringStringUnorderedMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(StringCharPtrUnorderedMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(StringStringMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(StringMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(StringCharPtrMap map) : value_(std::move(map)) {}

AnyValue::AnyValue(VectorString vec) : value_(std::move(vec)) {}

AnyValue::AnyValue(VectorCharPtr vec) : value_(std::move(vec)) {}

AnyValue::AnyValue(VectorStringAnyUnorderedMap vec) : value_(std::move(vec)) {}

AnyValue::AnyValue(VectorStringStringUnorderedMap vec) : value_(std::move(vec)) {}

AnyValue::AnyValue(std::shared_ptr<std::any> s) : value_(std::move(s)) {}

Type AnyValue::type() const
{
    if (is_null()) {
        return Type::Null;
    }
    if (is_bool()) {
        return Type::Bool;
    }
    if (is_float()) {
        return Type::Float;
    }
    if (is_string()) {
        return Type::String;
    }
    if (is_int32()) {
        return Type::Int32;
    }
    return Type::Null;
}

std::string AnyValue::typeName() const
{
    return "AnyValue";
}

}  // namespace jiuwen
