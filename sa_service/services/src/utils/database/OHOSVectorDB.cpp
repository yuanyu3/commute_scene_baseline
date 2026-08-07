/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "utils/database/OHOSVectorDB.h"
#include <relational_store_error_code.h>
#include "utils/camera_agent_log.h"

namespace database {

constexpr size_t COMMA_SPACE_LENGTH = 2;  // 逗号和空格的长度

std::string BuildVDBFieldDefinitionSql(const jiuwen::DBTableField& vdbField)
{
    std::string vdbFieldSql = vdbField.name + " ";

    // 根据vdbFieldType设置列类型
    switch (vdbField.type) {
        case jiuwen::FieldType::INT64:
            vdbFieldSql += "INTEGER";
            break;
        case jiuwen::FieldType::DOUBLE:
            vdbFieldSql += "REAL";
            break;
        case jiuwen::FieldType::STRING:
            vdbFieldSql += "TEXT";
            break;
        case jiuwen::FieldType::VECTOR_FLOAT:
            vdbFieldSql += "FLOATVECTOR(" + std::to_string(vdbField.vectorDim) + ")";  // 使用vectorDim动态设置向量长度
            break;
        default:
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::CreateTable] Unsupported Field Type for Field %{public}s",
                         vdbField.name.c_str());
            return "";
    }

    // 添加约束
    if (vdbField.constraint.isPrimaryKey) {
        vdbFieldSql += " PRIMARY KEY";
    }
    if (!vdbField.constraint.isNullable) {
        vdbFieldSql += " NOT NULL";
    }

    // 添加默认值约束
    if (vdbField.defaultValue.has_value()) {
        if (std::holds_alternative<int64_t>(vdbField.defaultValue.value())) {
            vdbFieldSql += " DEFAULT " + std::to_string(std::get<int64_t>(vdbField.defaultValue.value()));
        } else if (std::holds_alternative<double>(vdbField.defaultValue.value())) {
            vdbFieldSql += " DEFAULT " + std::to_string(std::get<double>(vdbField.defaultValue.value()));
        } else if (std::holds_alternative<std::string>(vdbField.defaultValue.value())) {
            vdbFieldSql += " DEFAULT '" + std::get<std::string>(vdbField.defaultValue.value()) + "'";
        } else if (std::holds_alternative<std::vector<float>>(vdbField.defaultValue.value())) {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::CreateTable] Vector Default Value Not Supported for Field %{public}s",
                         vdbField.name.c_str());
            return "";
        }
    }

    // 添加检查约束
    if (!vdbField.checkConstraints.empty()) {
        vdbFieldSql += " " + vdbField.checkConstraints;
    }
    return vdbFieldSql;
}

std::string ComposeInsertSqlQuery(const std::string& table, const jiuwen::DBRecord& record, OH_Data_Values* values)
{
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeInsertSqlQuery] OH_Data_Values Is NULL for Table %{public}s",
                     table.c_str());
        return "";
    }

    if (record.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeInsertSqlQuery] Insert Data Is Empty for Table %{public}s",
                     table.c_str());
        return "";
    }

    std::string insertSqlQuery = "INSERT INTO " + table + " ";
    std::string columnNames = "(";
    std::string insertParameters = "(";
    std::string separator = ", ";
    std::string questionMark = "?";

    for (const auto& [key, value] : record) {
        columnNames += (key + separator);
        insertParameters += (questionMark + separator);
        if (std::holds_alternative<int64_t>(value)) {
            OH_Values_PutInt(values, std::get<int64_t>(value));
        } else if (std::holds_alternative<std::vector<float>>(value)) {
            const std::vector<float>& vec = std::get<std::vector<float>>(value);
            OH_Values_PutFloatVector(values, vec.data(), vec.size());
        } else if (std::holds_alternative<std::string>(value)) {
            OH_Values_PutText(values, std::get<std::string>(value).c_str());
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeInsertSqlQuery] Unsupported Field Type in HarmonyRDB for Table %{public}s",
                table.c_str());
            break;
        }
    }
    if (columnNames.length() >= separator.size() &&
        columnNames.substr(columnNames.size() - separator.size()) == separator) {
        columnNames.resize(columnNames.size() - separator.size());
    }
    columnNames += ")";

    if (insertParameters.length() >= separator.size() &&
        insertParameters.substr(insertParameters.size() - separator.size()) == separator) {
        insertParameters.resize(insertParameters.size() - separator.size());
    }
    insertParameters += ");";
    insertSqlQuery += (columnNames + " VALUES " + insertParameters);
    return insertSqlQuery;
}

std::string ComposeDeleteSqlQuery(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions,
                                  OH_Data_Values* values)
{
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeDeleteSqlQuery] OH_Data_Values Is NULL for Table %{public}s",
                     table.c_str());
        return "";
    }

    if (conditions.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeDeleteSqlQuery] Condition Is Cannot, Cannot Be Executed for Table %{public}s",
            table.c_str());
        return "";
    }

    std::string deleteSqlQuery = "DELETE FROM " + table + " WHERE ";
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (std::holds_alternative<int64_t>(cond.value)) {
            OH_Values_PutInt(values, std::get<int64_t>(cond.value));
        } else if (std::holds_alternative<std::vector<float>>(cond.value)) {
            const std::vector<float>& vec = std::get<std::vector<float>>(cond.value);
            OH_Values_PutFloatVector(values, vec.data(), vec.size());
        } else if (std::holds_alternative<std::string>(cond.value)) {
            OH_Values_PutText(values, std::get<std::string>(cond.value).c_str());
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeDeleteSqlQuery] Unsupported Condition for Table %{public}s",
                         table.c_str());
            return "";
        }
        deleteSqlQuery += (cond.field + " " + cond.op + " ?");

        if (i != conditions.size() - 1) {
            deleteSqlQuery += " AND ";
        }
    }
    deleteSqlQuery += ";";
    return deleteSqlQuery;
}

std::string ComposeUpdateSqlQuery(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions,
                                  const jiuwen::DBRecord& updateData, OH_Data_Values* values)
{
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeUpdateSqlQuery] OH_Data_Values Is NULL for Table %{public}s",
                     table.c_str());
        return "";
    }

    if (conditions.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeUpdateSqlQuery] Condition Is Cannot, Cannot Be Executed for Table %{public}s",
            table.c_str());
        return "";
    }

    std::string updateSqlQuery = "UPDATE " + table + " SET ";
    std::string separator = ", ";
    for (const auto& [key, value] : updateData) {
        updateSqlQuery += (key + " = ?" + separator);
        if (std::holds_alternative<int64_t>(value)) {
            OH_Values_PutInt(values, std::get<int64_t>(value));
        } else if (std::holds_alternative<std::vector<float>>(value)) {
            const std::vector<float>& vec = std::get<std::vector<float>>(value);
            OH_Values_PutFloatVector(values, vec.data(), vec.size());
        } else if (std::holds_alternative<std::string>(value)) {
            OH_Values_PutText(values, std::get<std::string>(value).c_str());
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeUpdateSqlQuery] Unsupported Field Type in HarmonyRDB for Table %{public}s",
                table.c_str());
            return "";
        }
    }
    if (updateSqlQuery.length() >= separator.size() &&
        updateSqlQuery.substr(updateSqlQuery.size() - separator.size()) == separator) {
        updateSqlQuery.resize(updateSqlQuery.size() - separator.size());
    }
    updateSqlQuery += " WHERE ";
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (std::holds_alternative<int64_t>(cond.value)) {
            OH_Values_PutInt(values, std::get<int64_t>(cond.value));
        } else if (std::holds_alternative<std::vector<float>>(cond.value)) {
            const std::vector<float>& vec = std::get<std::vector<float>>(cond.value);
            OH_Values_PutFloatVector(values, vec.data(), vec.size());
        } else if (std::holds_alternative<std::string>(cond.value)) {
            OH_Values_PutText(values, std::get<std::string>(cond.value).c_str());
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeUpdateSqlQuery] Unsupported Condition for Table %{public}s",
                         table.c_str());
            return "";
        }
        updateSqlQuery += (cond.field + " " + cond.op + " ?");

        if (i != conditions.size() - 1) {
            updateSqlQuery += " AND ";
        }
    }
    updateSqlQuery += ";";
    return updateSqlQuery;
}

std::string ComposeGetSqlQuery(const jiuwen::VectorQueryParams& queryParams, OH_Data_Values* values)
{
    std::string getSqlQuery = "SELECT ";
    bool hasVectorQuery = !queryParams.queryVector.empty();
    bool hasFilterConditions = !queryParams.filterConditions.empty();

    // 添加字段
    if (hasVectorQuery) {
        getSqlQuery += "* FROM " + queryParams.table + " WHERE ";
    } else {
        for (size_t i = 0; i < queryParams.fields.size(); ++i) {
            getSqlQuery += queryParams.fields[i];
            if (i != queryParams.fields.size() - 1) {
                getSqlQuery += ", ";
            }
        }
        getSqlQuery += " FROM " + queryParams.table;
        if (hasFilterConditions) {
            getSqlQuery += " WHERE ";
        }
    }

    // 添加向量查询条件
    if (hasVectorQuery) {
        getSqlQuery += queryParams.vectorField;
        if (queryParams.metric == "cosine") {
            getSqlQuery += " <=> ? < ";
        } else if (queryParams.metric == "l2") {
            getSqlQuery += " <-> ? < ";
        } else {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeGetSqlQuery] Unsupported Distance Function for Table %{public}s",
                         queryParams.table.c_str());
            return "";
        }
        std::string threshold = std::to_string(queryParams.simThreshold);
        getSqlQuery += threshold;
        OH_Values_PutFloatVector(values, queryParams.queryVector.data(), queryParams.queryVector.size());
    }

    // 添加普通查询条件
    if (hasFilterConditions) {
        if (hasVectorQuery) {
            getSqlQuery += " AND ";
        }
        for (size_t i = 0; i < queryParams.filterConditions.size(); ++i) {
            const auto& cond = queryParams.filterConditions[i];
            if (std::holds_alternative<int64_t>(cond.value)) {
                OH_Values_PutInt(values, std::get<int64_t>(cond.value));
            } else if (std::holds_alternative<std::vector<float>>(cond.value)) {
                const std::vector<float>& vec = std::get<std::vector<float>>(cond.value);
                OH_Values_PutFloatVector(values, vec.data(), vec.size());
            } else if (std::holds_alternative<std::string>(cond.value)) {
                OH_Values_PutText(values, std::get<std::string>(cond.value).c_str());
            } else {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::ComposeGetSqlQuery] Unsupported Condition");
                return "";
            }
            getSqlQuery += (cond.field + " " + cond.op + " ?");

            if (i != queryParams.filterConditions.size() - 1) {
                getSqlQuery += " AND ";
            }
        }
    }

    // 添加LIMIT条件
    if (hasVectorQuery) {
        std::string limit = std::to_string(queryParams.top_k);
        getSqlQuery += " LIMIT " + limit;
    }

    getSqlQuery += ";";
    return getSqlQuery;
}

bool SaveColumnDataToRecord(const std::string& field, size_t columnIndex, jiuwen::DBRecord& record, OH_Cursor* cursor)
{
    OH_ColumnType columnType;
    int ret = cursor->getColumnType(cursor, columnIndex, &columnType);
    if (ret != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get Column Type Failed");
        return false;
    }

    // 根据类型安全获取数据
    switch (columnType) {
        case TYPE_INT64: {
            int64_t intContainer;
            ret = cursor->getInt64(cursor, columnIndex, &intContainer);
            if (ret != OH_Rdb_ErrCode::RDB_OK) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get Int Type Data Failed");
                return false;
            }
            record.emplace(field, intContainer);
            break;
        }
        case TYPE_FLOAT_VECTOR: {
            size_t floatVectorLen;
            ret = OH_Cursor_GetFloatVectorCount(cursor, columnIndex, &floatVectorLen);
            if (ret != OH_Rdb_ErrCode::RDB_OK) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get Size Of Float Vector Failed");
                return false;
            }

            std::vector<float> floatVectorContainer(floatVectorLen);
            size_t floatVectorContainerLen;
            ret = OH_Cursor_GetFloatVector(cursor, columnIndex, floatVectorContainer.data(), floatVectorLen,
                                           &floatVectorContainerLen);
            if (ret != OH_Rdb_ErrCode::RDB_OK) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get Float Vector Type Data Failed");
                return false;
            }
            record.emplace(field, floatVectorContainer);
            break;
        }
        case TYPE_TEXT: {
            size_t stringContainerLen;
            ret = cursor->getSize(cursor, columnIndex, &stringContainerLen);
            if (ret != OH_Rdb_ErrCode::RDB_OK) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get Size Of Text Failed");
                return false;
            }

            std::vector<char> stringContainer(stringContainerLen, 0);
            ret = cursor->getText(cursor, columnIndex, stringContainer.data(), stringContainerLen + 1);
            if (ret != OH_Rdb_ErrCode::RDB_OK) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::SaveColumnDataToRecord] Get String Type Data Failed");
                return false;
            }
            record.emplace(field, std::string(stringContainer.data()));
            break;
        }
        default:
            CAMERA_AGENT_LOG_ERROR("[OHOSRelationalDB::SaveColumnDataToRecord] Unsupported Column Type");
            return false;
    }
    return true;
}

OHOSVectorDB::OHOSVectorDB(const jiuwen::DBConfig& dbConfig) : jiuwen::BaseDB(dbConfig)
{
    bool isInitSuccess = Init(dbConfig);
    if (isInitSuccess) {
        CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::OHOSVectorDB]: Init Success");
    } else {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::OHOSVectorDB]: Init Failed");
    }
}

OHOSVectorDB::~OHOSVectorDB()
{
    if (handler_ != nullptr) {
        int errCode = OH_Rdb_CloseStore(handler_);
        if (errCode != OH_Rdb_ErrCode::RDB_OK) {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::~OHOSVectorDB]: Failed To Destroy Database handler");
        }
        handler_ = nullptr;
    }
}

bool OHOSVectorDB::Init(const jiuwen::DBConfig& dbConfig)
{
    // 直接从 dbConfig 中获取 dbName 和 databaseDir，因为已经确保不为空
    auto dbName = dbConfig.dbName;
    auto dbDir = dbConfig.databaseDir;

    auto bundleIter = dbConfig.optionalConf.find("bundleName");
    if (bundleIter == dbConfig.optionalConf.end()) {
        return false;
    }
    auto bundleName = bundleIter->second.get<std::string>().value();

    auto moduleIter = dbConfig.optionalConf.find("moduleName");
    if (moduleIter == dbConfig.optionalConf.end()) {
        return false;
    }

    OH_Rdb_ConfigV2* config = OH_Rdb_CreateConfig();
    OH_Rdb_SetDatabaseDir(config, dbDir.c_str());
    OH_Rdb_SetStoreName(config, dbName.c_str());
    OH_Rdb_SetBundleName(config, bundleName.c_str());
    OH_Rdb_SetEncrypted(config, false);
    OH_Rdb_SetSecurityLevel(config, OH_Rdb_SecurityLevel::S1);
    OH_Rdb_SetArea(config, RDB_SECURITY_AREA_EL1);
    OH_Rdb_SetDbType(config, RDB_CAYLEY);

    // 创建/打开数据库实例
    int errCode = 0;
    handler_ = OH_Rdb_CreateOrOpen(config, &errCode);
    if (errCode != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Init] OH_Rdb_CreateOrOpen failed");
        return false;
    }

    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Init]: Init Success");
    return true;
}

// 结构化建表参数
bool OHOSVectorDB::CreateTable(const jiuwen::DBTableSchema& tableSchema)
{
    // 构建CREATE TABLE语句
    std::string createTableSql = "CREATE TABLE IF NOT EXISTS " + tableSchema.tableName + " (";

    // 添加字段定义
    bool allFieldsProcessed = true;
    for (const auto& field : tableSchema.fields) {
        std::string fieldSql = BuildVDBFieldDefinitionSql(field);
        if (fieldSql.empty()) {
            allFieldsProcessed = false;
            break;
        }
        createTableSql += fieldSql + ", ";
    }

    if (!allFieldsProcessed) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::CreateTable] Failed to process fields for table %{public}s",
                     tableSchema.tableName.c_str());
        return false;
    }

    // 移除最后一个逗号和空格
    if (tableSchema.fields.size() > 0) {
        createTableSql.resize(createTableSql.size() - COMMA_SPACE_LENGTH);
    }

    createTableSql += ");";

    // 执行建表SQL
    int errCode = OH_Rdb_ExecuteByTrxId(handler_, 0, createTableSql.c_str());
    if (errCode != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::CreateTable] Create Table %{public}s Failed",
                     tableSchema.tableName.c_str());
        return false;
    }
    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::CreateTable] Create Table %{public}s Success", tableSchema.tableName.c_str());
    return true;
}

bool OHOSVectorDB::Add(const std::string& table, const jiuwen::DBRecords& records)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Add] Add Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Add] Add Record Failed, Table Name Is Empty");
        return false;
    }

    if (records.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Add] Add Record Failed, Record Is Empty");
        return false;
    }

    for (const auto& record : records) {
        OH_Data_Values* values = OH_Values_Create();
        if (values == nullptr) {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Add] Failed to Create OH_Data_Values for Table %{public}s",
                         table.c_str());
            return false;
        }
        auto insertSql = ComposeInsertSqlQuery(table, record, values);
        int ret = OH_Rdb_ExecuteV2(handler_, insertSql.c_str(), values, nullptr);
        if (ret != OH_Rdb_ErrCode::RDB_OK) {
            CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Add] Add Failed for Table %{public}s", table.c_str());
            OH_Values_Destroy(values);
            return false;
        }
        OH_Values_Destroy(values);
    }
    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Add] Add Success for Table %{public}s", table.c_str());
    return true;
}

bool OHOSVectorDB::Delete(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Delete] Delete Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Delete] Delete Record Failed, Table Name Is Empty");
        return false;
    }

    OH_Data_Values* values = OH_Values_Create();
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Delete] Failed to Create OH_Data_Values for Table %{public}s",
                     table.c_str());
        return false;
    }

    std::string deleteSqlQuery = ComposeDeleteSqlQuery(table, conditions, values);
    int ret = OH_Rdb_ExecuteV2(handler_, deleteSqlQuery.c_str(), values, nullptr);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Delete] Delete Failed for Table %{public}s", table.c_str());
        OH_Values_Destroy(values);
        return false;
    }

    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Delete] Delete Success for Table %{public}s", table.c_str());
    OH_Values_Destroy(values);
    return true;
}

bool OHOSVectorDB::Update(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions,
                          const jiuwen::DBRecord& updateData)
{
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Update] Update Record Failed, Try to Init Instance at First");
        return false;
    }

    if (table.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Update] Update Record Failed, Table Name Is Empty");
        return false;
    }

    OH_Data_Values* values = OH_Values_Create();
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Update] Failed to Create OH_Data_Values for Table %{public}s",
                     table.c_str());
        return false;
    }

    std::string updateSqlQuery = ComposeUpdateSqlQuery(table, conditions, updateData, values);

    int ret = OH_Rdb_ExecuteV2(handler_, updateSqlQuery.c_str(), values, nullptr);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Update] Update Record Failed for Table %{public}s", table.c_str());
        OH_Values_Destroy(values);
        return false;
    }

    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Update] Update Record Success for Table %{public}s", table.c_str());
    OH_Values_Destroy(values);
    return true;
}

jiuwen::DBRecords OHOSVectorDB::Query(const jiuwen::QueryParams& queryParams)
{
    // 检查数据库处理器是否有效
    if (handler_ == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Query Failed, Try to Init Instance at First");
        return {};
    }

    // 检查查询参数类型是否为关系型查询参数
    if (queryParams.params.index() != 1) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Unsupported Query Type");
        return {};
    }

    // 获取向量型查询参数
    const int dbTypeIndex = static_cast<int>(jiuwen::DBType::VDB);
    const auto& vectorParams = std::get<dbTypeIndex>(queryParams.params);

    if (vectorParams.fields.empty()) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Fields Are Empty");
        return {};
    }

    OH_Data_Values* values = OH_Values_Create();
    if (values == nullptr) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Failed to Create OH_Data_Values for Table %{public}s",
                     vectorParams.table.c_str());
        return {};
    }
    std::string getSqlQuery = ComposeGetSqlQuery(vectorParams, values);

    OH_Cursor* cursor = OH_Rdb_ExecuteQueryV2(handler_, getSqlQuery.c_str(), values);
    OH_Values_Destroy(values);

    int rowCount = 0;
    int errCode = cursor->getRowCount(cursor, &rowCount);
    if (errCode != OH_Rdb_ErrCode::RDB_OK) {
        CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Query Failed for Table %{public}s", vectorParams.table.c_str());
        return {};
    }

    if (errCode == OH_Rdb_ErrCode::RDB_OK && rowCount == 0) {
        CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Query] Query Success But Return 0 Row for Table %{public}s",
                    vectorParams.table.c_str());
        return {};
    }

    jiuwen::DBRecords result;
    while (cursor != nullptr && cursor->goToNextRow(cursor) == OH_Rdb_ErrCode::RDB_OK) {
        jiuwen::DBRecord record;
        for (size_t i = 0; i < vectorParams.fields.size(); ++i) {
            const auto& field = vectorParams.fields[i];
            if (!SaveColumnDataToRecord(field, i, record, cursor)) {
                CAMERA_AGENT_LOG_ERROR("[OHOSVectorDB::Query] Get Column Data Failed for Table %{public}s",
                             vectorParams.table.c_str());
                return {};
            }
        }
        result.emplace_back(record);
    }
    CAMERA_AGENT_LOG_INFO("[OHOSVectorDB::Query] Query Success And Return %{public}d Rows for Table %{public}s",
                rowCount, vectorParams.table.c_str());
    if (cursor != nullptr) {
        cursor->destroy(cursor);
    }
    return result;
}

}  // namespace database