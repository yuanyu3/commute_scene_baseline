/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef JIUWEN_LITE_AGENTS_API20_OHOS_VECTORDB_H
#define JIUWEN_LITE_AGENTS_API20_OHOS_VECTORDB_H

#include <any>
#include <oh_cursor.h>
#include <relational_store.h>
#include <relational_store_error_code.h>
#include "BaseDB.h"

namespace database {
class OHOSVectorDB : public jiuwen::BaseDB {
public:
    explicit OHOSVectorDB(const jiuwen::DBConfig& dbConfig);
    
    ~OHOSVectorDB() override;

    bool Init(const jiuwen::DBConfig& dbConfig) override;

    // 创建表结构
    bool CreateTable(const jiuwen::DBTableSchema& tableSchema) override;

    // 新增记录
    bool Add(const std::string& table, const jiuwen::DBRecords& records) override;

    // 更新记录（根据条件更新）
    bool Update(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions,
                const jiuwen::DBRecord& updateData) override;

    // 删除记录（根据条件）
    bool Delete(const std::string& table, const std::vector<jiuwen::DBCondition>& conditions) override;

    // 条件查询（返回匹配的记录）
    jiuwen::DBRecords Query(const jiuwen::QueryParams& queryParams) override;

private:
    OH_Rdb_Store* handler_;
};
}  // namespace database

#endif  // JIUWEN_LITE_AGENTS_API20_OHOS_VECTORDB_H
