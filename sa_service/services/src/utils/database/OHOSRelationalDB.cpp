/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "utils/database/OHOSRelationalDB.h"
#include "utils/camera_agent_log.h"

namespace database {

constexpr size_t COMMA_SPACE_LENGTH = 2;  // 逗号和空格的长度

OH_VBucket* ConvertToValueBucket(const jiuwen::DBRecord& record)
{
    OH_VBucket* bucket = OH_Rdb_CreateValuesBucket();
    if (bucket == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::ConvertToValueBucket] ConvertToValueBucket Failed");
        return nullptr;
    }
    for (const auto& [key, value] : record) {
        if (std::holds_alternative<int64_t>(value)) {
            // 官网：int64_t → PutLong
            bucket->putInt64(bucket, key.c_str(), std::get<int64_t>(value));
        } else if (std::holds_alternative<double>(value)) {
            // 官网：double → PutDouble
            bucket->putReal(bucket, key.c_str(), std::get<double>(value));
        } else if (std::holds_alternative<std::string>(value)) {
            // 官网：std::string → PutString
            bucket->putText(bucket, key.c_str(), std::get<std::string>(value).c_str());
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::ConvertToValueBucket] Unsupported Field Type in HarmonyRDB! Field: %{public}s",
                key.c_str());
            bucket->destroy(bucket);
            return nullptr;
        }
    }
    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::ConvertToValueBucket] ConvertToValueBucket Success");
    return bucket;
}

// 辅助函数：根据DBCondition的操作符，映射到Predicates的对应方法
bool BindConditionToPredicates(OH_Predicates* predicates, const jiuwen::DBCondition& cond)
{
    if (predicates == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::BindConditionToPredicates] Predicates Is NULL");
        return false;
    }

    // 创建OH_VObject存储条件值（官网要求：条件值必须通过OH_VObject传入）
    OH_VObject* valueObj = OH_Rdb_CreateValueObject();
    if (valueObj == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::BindConditionToPredicates] Create OH_VObject Failed");
        return false;
    }

    // 绑定条件值（根据DBValue类型映射到OH_VObject的对应方法，官网类型严格匹配）
    // bool valueBindSuccess = true;

    if (std::holds_alternative<int64_t>(cond.value)) {
        int64_t val = std::get<int64_t>(cond.value);
        valueObj->putInt64(valueObj, &val, 1);  // 官网接口：putInt64(OH_VObject*, const int64_t*, uint32_t)
    } else if (std::holds_alternative<double>(cond.value)) {
        double val = std::get<double>(cond.value);
        valueObj->putDouble(valueObj, &val, 1);  // 官网接口：putDouble(OH_VObject*, const double*, uint32_t)
    } else if (std::holds_alternative<std::string>(cond.value)) {
        const std::string& val = std::get<std::string>(cond.value);
        valueObj->putText(valueObj, val.c_str());  // 官网接口：putText(OH_VObject*, const char*)
    } else {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::BindConditionToPredicates] Unsupported Condition for Field %{public}s",
                     cond.field.c_str());
        valueObj->destroy(valueObj);
        return false;
    }

    // 根据操作符调用Predicates对应方法（官网支持的核心操作符映射）
    if (cond.op == "=") {
        predicates->equalTo(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == "!=") {
        predicates->notEqualTo(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == ">") {
        predicates->greaterThan(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == ">=") {
        predicates->greaterThanOrEqualTo(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == "<") {
        predicates->lessThan(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == "<=") {
        predicates->lessThanOrEqualTo(predicates, cond.field.c_str(), valueObj);
    } else if (cond.op == "like") {
        predicates->like(predicates, cond.field.c_str(), valueObj);
    } else {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::BindConditionToPredicates] Unsupported Operator %{public}s for Field %{public}s",
            cond.op.c_str(), cond.field.c_str());
        valueObj->destroy(valueObj);
        return false;
    }

    // 官网要求：OH_VObject使用后必须手动销毁
    valueObj->destroy(valueObj);
    return true;
}

// 构建字段定义SQL
std::string BuildRDBFieldDefinitionSql(const jiuwen::DBTableField& rdbField)
{
    std::string rdbFieldSql = rdbField.name + " ";

    // 根据rdbFieldType设置列类型
    switch (rdbField.type) {
        case jiuwen::FieldType::STRING:
            rdbFieldSql += "TEXT";
            break;
        case jiuwen::FieldType::DOUBLE:
            rdbFieldSql += "REAL";
            break;
        case jiuwen::FieldType::INT64:
            rdbFieldSql += "INTEGER";
            break;
        default:
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::CreateTable] Unsupported Field Type for Field %{public}s",
                         rdbField.name.c_str());
            return "";
    }

    // 添加约束
    if (rdbField.constraint.isPrimaryKey) {
        rdbFieldSql += " PRIMARY KEY";
    }
    if (!rdbField.constraint.isNullable) {
        rdbFieldSql += " NOT NULL";
    }

    // 添加默认值约束
    if (rdbField.defaultValue.has_value()) {
        if (std::holds_alternative<int64_t>(rdbField.defaultValue.value())) {
            rdbFieldSql += " DEFAULT " + std::to_string(std::get<int64_t>(rdbField.defaultValue.value()));
        } else if (std::holds_alternative<double>(rdbField.defaultValue.value())) {
            rdbFieldSql += " DEFAULT " + std::to_string(std::get<double>(rdbField.defaultValue.value()));
        } else if (std::holds_alternative<std::string>(rdbField.defaultValue.value())) {
            rdbFieldSql += " DEFAULT '" + std::get<std::string>(rdbField.defaultValue.value()) + "'";
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::CreateTable] Default Value Not Supported for Field %{public}s",
                         rdbField.name.c_str());
            return "";
        }
    }

    // 添加检查约束
    if (!rdbField.checkConstraints.empty()) {
        rdbFieldSql += " " + rdbField.checkConstraints;
    }

    return rdbFieldSql;
}

// 新增全局函数：处理单行数据的查询结果
bool ProcessSingleRow(OH_Cursor* cursor, const std::vector<std::string>& fields, jiuwen::DBRecord& record)
{
    for (size_t i = 0; i < fields.size(); ++i) {
        const char* field = fields[i].c_str();

        // 获取列类型
        OH_ColumnType columnType;
        int ret = cursor->getColumnType(cursor, i, &columnType);
        if (ret != OH_Rdb_ErrCode::RDB_OK) {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Failed To Get Column Type");
            continue;
        }

        // 根据类型安全获取数据
        switch (columnType) {
            case TYPE_INT64: {
                int64_t intValue;
                ret = cursor->getInt64(cursor, i, &intValue);
                if (ret != OH_Rdb_ErrCode::RDB_OK) {
                    CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] getInt64 Failed");
                    return false;
                }
                record[field] = jiuwen::DBValue(intValue);
                break;
            }
            case TYPE_REAL: {
                double doubleValue;
                ret = cursor->getReal(cursor, i, &doubleValue);
                if (ret != OH_Rdb_ErrCode::RDB_OK) {
                    CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] getReal Failed");
                    return false;
                }
                record[field] = jiuwen::DBValue(doubleValue);
                break;
            }
            case TYPE_TEXT: {
                size_t stringContainerLen;
                ret = cursor->getSize(cursor, i, &stringContainerLen);
                if (ret != OH_Rdb_ErrCode::RDB_OK) {
                    CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] getSize Failed");
                    return false;
                }

                std::vector<char> stringContainer(stringContainerLen, 0);
                ret = cursor->getText(cursor, i, stringContainer.data(), stringContainerLen + 1);
                if (ret != OH_Rdb_ErrCode::RDB_OK) {
                    CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Get String Type Data Failed");
                    return false;
                }
                record.emplace(field, std::string(stringContainer.data()));
                break;
            }
            default:
                CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Unsupported Column Type");
                break;
        }
    }
    return true;
}

// 新增全局函数：处理查询结果集
bool ProcessQueryResults(OH_Cursor* cursor, const std::vector<std::string>& fields, jiuwen::DBRecords& result)
{
    while (cursor != nullptr && cursor->goToNextRow(cursor) == OH_Rdb_ErrCode::RDB_OK) {
        jiuwen::DBRecord record;
        if (!ProcessSingleRow(cursor, fields, record)) {
            return false;
        }
        result.push_back(std::move(record));  // 移动语义优化
    }
    return true;
}

OHOSRelationalDB::OHOSRelationalDB(const jiuwen::DBConfig& dbConfig) : jiuwen::BaseDB(dbConfig)
{
    bool isInitSuccess = Init(dbConfig);
    if (isInitSuccess) {
        CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::OHOSRelationalDB]: Init Success");
    } else {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::OHOSRelationalDB]: Init Failed");
    }
}

OHOSRelationalDB::~OHOSRelationalDB()
{
    if (handler_ != nullptr) {
        // 关闭数据库连接
        int errCode = OH_Rdb_CloseStore(handler_);
        if (errCode != OH_Rdb_ErrCode::RDB_OK) {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::~OHOSRelationalDB] Failed to Close The Database Handler");
        }
        handler_ = nullptr;
    }
}

bool OHOSRelationalDB::Init(const jiuwen::DBConfig& dbConfig)
{
    // 判断字段是否为空
    if (dbConfig.dbName.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Init] dbName Is Empty");
        return false;
    }
    auto dbName = dbConfig.dbName;

    if (dbConfig.databaseDir.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Init] databaseDir Is Empty");
        return false;
    }
    auto dbDir = dbConfig.databaseDir;

    auto bundleIter = dbConfig.optionalConf.find("bundleName");
    if (bundleIter == dbConfig.optionalConf.end()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Init] bundleName Is Empty");
        return false;
    }
    auto moduleIter = dbConfig.optionalConf.find("moduleName");
    if (moduleIter == dbConfig.optionalConf.end()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Init] moduleName Is Empty");
        return false;
    }

    std::string bundleName;
    std::string moduleName;
    if (bundleIter->second.has_value() && moduleIter->second.has_value()) {
        bundleName = bundleIter->second.get<std::string>().value();
        moduleName = moduleIter->second.get<std::string>().value();
    }

    OH_Rdb_Config config;
    config.storeName = dbName.c_str();
    config.dataBaseDir = dbDir.c_str();
    config.bundleName = bundleName.c_str();
    config.moduleName = moduleName.c_str();
    config.securityLevel = OH_Rdb_SecurityLevel::S1;
    config.isEncrypt = false;
    config.selfSize = sizeof(OH_Rdb_Config);
    config.area = RDB_SECURITY_AREA_EL1;

    // 创建/打开数据库实例
    int errCode = 0;
    handler_ = OH_Rdb_GetOrOpen(&config, &errCode);
    if (errCode != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Init] OH_Rdb_CreateOrOpen failed");
        return false;
    }
    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::Init]: Init Success");
    return true;
}

// 结构化建表参数
bool OHOSRelationalDB::CreateTable(const jiuwen::DBTableSchema& tableSchema)
{
    // 构建CREATE TABLE语句
    std::string createTableSql = "CREATE TABLE IF NOT EXISTS " + tableSchema.tableName + " (";

    // 添加字段定义
    bool allFieldsProcessed = true;
    for (const auto& field : tableSchema.fields) {
        std::string fieldSql = BuildRDBFieldDefinitionSql(field);
        if (fieldSql.empty()) {
            allFieldsProcessed = false;
            break;
        }
        createTableSql += fieldSql + ", ";
    }

    if (!allFieldsProcessed) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::CreateTable] Failed to process fields for table %{public}s",
                     tableSchema.tableName.c_str());
        return false;
    }

    // 移除最后一个逗号和空格
    if (tableSchema.fields.size() > 0) {
        createTableSql.resize(createTableSql.size() - COMMA_SPACE_LENGTH);
    }

    createTableSql += ");";

    // 执行建表SQL
    int errCode = OH_Rdb_Execute(handler_, createTableSql.c_str());
    if (errCode != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::CreateTable] Create Table %{public}s Failed",
                     tableSchema.tableName.c_str());
        return false;
    }
    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::CreateTable] Create Table %{public}s Success",
                tableSchema.tableName.c_str());
    return true;
}

bool OHOSRelationalDB::Add(const std::string& table, const jiuwen::DBRecords& records)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Add] Add Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Add] Add Record Failed, Table Name Is Empty");
        return false;
    }

    if (records.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Add] Add Record Failed, Records Is Empty");
        return false;
    }

    for (const auto& record : records) {
        auto bucket = ConvertToValueBucket(record);
        if (bucket == nullptr) {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Add] ConvertToValueBucket Failed, Check If Record Is Valid for Table %{public}s",
                table.c_str());
            return false;
        }
        int rowId = OH_Rdb_Insert(handler_, table.c_str(), bucket);
        bucket->destroy(bucket);
        if (rowId < 0) {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Add] Insert into Table %{public}s Failed", table.c_str());
            return false;
        }
    }
    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::Add] Add Success for Table %{public}s", table.c_str());
    return true;
}

bool OHOSRelationalDB::Delete(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Delete Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Delete Record Failed, Table Name Is Empty");
        return false;
    }

    if (conditions.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Delete Record Failed, Conditions Is Empty");
        return false;
    }

    OH_Predicates* predicates = OH_Rdb_CreatePredicates(table.c_str());
    if (predicates == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Delete Record Failed, Create Predicates Failed for Table %{public}s",
                     table.c_str());
        return false;
    }

    bool allConditionsBindSuccess = true;
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (!BindConditionToPredicates(predicates, cond)) {
            allConditionsBindSuccess = false;
            break;
        }
        // 多条件时，除第一个外，添加AND连接（官网支持andOperate/orOperate链式调用）
        if (i < conditions.size() - 1) {
            predicates->andOperate(predicates);
        }
    }

    if (!allConditionsBindSuccess) {
        predicates->destroy(predicates);  // 条件绑定失败，释放Predicates
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Bind Predicates Failed for Table %{public}s", table.c_str());
        return false;
    }

    int deleteRows = OH_Rdb_Delete(handler_, predicates);

    predicates->destroy(predicates);

    if (deleteRows < 0) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Delete] Delete Record Failed for Table %{public}s", table.c_str());
        return false;
    }
    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::Delete] Delete Record Success for Table %{public}s", table.c_str());
    return true;
}

bool OHOSRelationalDB::Update(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions,
                              const jiuwen::DBRecord& updateData)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Update Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Update Record Failed, Table Name Is Empty");
        return false;
    }

    if (conditions.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Update Record Failed, Conditions Is Empty");
        return false;
    }

    if (updateData.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Update Record Failed, UpdateData Is Empty");
        return false;
    }

    // 创建OH_VBucket存储更新数据
    OH_VBucket* bucket = ConvertToValueBucket(updateData);
    if (bucket == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] ConvertToValueBucket Failed, Check If Update Data Is Valid for Table "
                     "%{public}s",
                     table.c_str());
        return false;
    }

    // 创建OH_Predicates存储条件
    OH_Predicates* predicates = OH_Rdb_CreatePredicates(table.c_str());
    if (predicates == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Create Predicates Failed for Table %{public}s",
                     table.c_str());
        bucket->destroy(bucket);
        return false;
    }

    // 绑定条件
    bool allConditionsBindSuccess = true;
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (!BindConditionToPredicates(predicates, cond)) {
            allConditionsBindSuccess = false;
            break;
        }
        // 多条件时，除第一个外，添加AND连接
        if (i < conditions.size() - 1) {
            predicates->andOperate(predicates);
        }
    }

    if (!allConditionsBindSuccess) {
        predicates->destroy(predicates);
        bucket->destroy(bucket);
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Bind Predicates Failed for Table %{public}s", table.c_str());
        return false;
    }

    // 执行更新操作
    int updateRows = OH_Rdb_Update(handler_, bucket, predicates);

    // 释放资源
    predicates->destroy(predicates);
    bucket->destroy(bucket);

    if (updateRows < 0) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Update] Update Record Failed for Table %{public}s", table.c_str());
        return false;
    }

    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::Update] Update Record Success for Table %{public}s", table.c_str());
    return true;
}

jiuwen::DBRecords OHOSRelationalDB::Query(const jiuwen::QueryParams& queryParams)
{
    jiuwen::DBRecords result;

    // 检查数据库处理器是否有效
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Query Failed, Try to Init Instance at First");
        return result;
    }

    // 检查查询参数类型是否为关系型查询参数
    if (queryParams.params.index() != 0) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Unsupported Query Type");
        return result;
    }

    // 获取关系型查询参数
    constexpr int DBTypeIndex = static_cast<int>(jiuwen::DBType::RDB);
    const auto& relationalParams = std::get<DBTypeIndex>(queryParams.params);

    // 创建查询条件
    if (relationalParams.table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Table Name Is Empty");
        return result;
    }
    OH_Predicates* predicates = OH_Rdb_CreatePredicates(relationalParams.table.c_str());
    if (predicates == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Create Predicates Failed for Table %{public}s",
                     relationalParams.table.c_str());
        return result;
    }

    // 绑定查询条件
    for (const auto& cond : relationalParams.where) {
        if (!BindConditionToPredicates(predicates, cond)) {
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Bind Condition Failed for Table %{public}s",
                         relationalParams.table.c_str());
            predicates->destroy(predicates);
            return result;
        }
    }

    // 创建查询字段列表
    std::vector<const char*> columns;
    for (const auto& field : relationalParams.fields) {
        columns.push_back(field.c_str());
    }

    // 执行查询
    OH_Cursor* cursor = OH_Rdb_Query(handler_, predicates, columns.data(), columns.size());
    if (cursor == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Query Execution Failed for Table %{public}s",
                     relationalParams.table.c_str());
        predicates->destroy(predicates);
        return result;
    }

    // 处理查询结果
    if (!ProcessQueryResults(cursor, relationalParams.fields, result)) {
        CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::Query] Process Query Results Failed for Table %{public}s",
                     relationalParams.table.c_str());
    }

    // 释放资源
    if (cursor != nullptr) {
        cursor->destroy(cursor);
    }
    predicates->destroy(predicates);

    CAMERA_AGENT_LOG_INFO("[OHOSRelationalDB::Query] Query Success for Table %{public}s",
                relationalParams.table.c_str());
    return result;
}

}  // namespace database