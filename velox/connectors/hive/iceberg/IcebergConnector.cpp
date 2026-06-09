/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/connectors/hive/iceberg/IcebergConnector.h"

#include "velox/connectors/hive/HiveConnector.h"
#include "velox/connectors/hive/iceberg/IcebergConfig.h"
#include "velox/connectors/hive/iceberg/IcebergDataSink.h"
#include "velox/connectors/hive/iceberg/IcebergDataSource.h"

namespace facebook::velox::connector::hive::iceberg {

namespace {

// Registers Iceberg partition transform functions with prefix.
// NOTE: These functions are registered for internal transform usage only.
// Upstream engines such as Prestissimo and Gluten should register the same
// functions with different prefixes to avoid conflicts.
void registerIcebergInternalFunctions(const std::string& prefix) {
  static std::once_flag registerFlag;

  std::call_once(registerFlag, [prefix]() {
    functions::iceberg::registerFunctions(prefix);
  });
}

// Helper function to extract data columns from changelog schema.
// If outputType contains a 'rowdata' field (changelog schema), extracts
// the ROW type from that field. Otherwise returns outputType unchanged.
RowTypePtr extractDataColumnsFromChangelogSchema(const RowTypePtr& outputType) {
  auto rowdataIdx = outputType->getChildIdxIfExists("rowdata");
  if (rowdataIdx.has_value()) {
    // This is a changelog schema - extract the data columns from rowdata field
    auto rowdataType = std::dynamic_pointer_cast<const RowType>(
        outputType->childAt(*rowdataIdx));
    VELOX_CHECK_NOT_NULL(
        rowdataType,
        "rowdata field must be a ROW type in changelog schema");
    return rowdataType;
  }
  // Not a changelog schema - return as is
  return outputType;
}

// Helper function to extract data column assignments from tableHandle's
// dataColumns
//ColumnHandleMap extractDataColumnAssignments(const RowTypePtr& dataSchema) {
//
//  // Get the field IDs from rowdata's children
//  const auto& rowdataFieldId = rowdataHandle->field();
//  const auto& childFieldIds = rowdataFieldId.children;
//
//  VELOX_CHECK_EQ(
//      childFieldIds.size(),
//      dataSchema->size(),
//      "Number of field IDs in rowdata must match number of data columns");
//
//  // Create new column handles for each data column
//  ColumnHandleMap dataAssignments;
//  for (size_t i = 0; i < dataSchema->size(); ++i) {
//    const auto& columnName = dataSchema->nameOf(i);
//    const auto& columnType = dataSchema->childAt(i);
//
//    auto dataColumnHandle = std::make_shared<IcebergColumnHandle>(
//        columnName,
//        HiveColumnHandle::ColumnType::kRegular,
//        columnType,
//        childFieldIds[i]);
//
//    dataAssignments[columnName] = dataColumnHandle;
//  }
//
//  return dataAssignments;
//}

} // namespace

IcebergConnector::IcebergConnector(
    const std::string& id,
    std::shared_ptr<const config::ConfigBase> config,
    folly::Executor* ioExecutor)
    : HiveConnector(id, config, ioExecutor),
      icebergConfig_(std::make_shared<IcebergConfig>(connectorConfig())) {
  registerIcebergInternalFunctions(icebergConfig_->functionPrefix());
}

std::unique_ptr<DataSource> IcebergConnector::createDataSource(
    const RowTypePtr& outputType,
    const ConnectorTableHandlePtr& tableHandle,
    const ColumnHandleMap& columnHandles,
    ConnectorQueryCtx* connectorQueryCtx) {
  auto icebergTableHandle = std::dynamic_pointer_cast<const HiveTableHandle>(tableHandle);
  VELOX_CHECK_NOT_NULL(icebergTableHandle, "IcebergTableHandle is null");

  if (icebergTableHandle->isChangelogQuery()) {
    return std::make_unique<IcebergDataSource>(
        icebergTableHandle->dataColumns(),
        tableHandle,
        icebergTableHandle->getDataColumnHandles(),
        &fileHandleFactory_,
        ioExecutor_,
        connectorQueryCtx,
        hiveConfig_,
        outputType,
        columnHandles);
  }
  return std::make_unique<IcebergDataSource>(
      outputType,
      tableHandle,
      columnHandles,
      &fileHandleFactory_,
      ioExecutor_,
      connectorQueryCtx,
      hiveConfig_);
}

std::unique_ptr<DataSink> IcebergConnector::createDataSink(
    RowTypePtr inputType,
    ConnectorInsertTableHandlePtr connectorInsertTableHandle,
    ConnectorQueryCtx* connectorQueryCtx,
    CommitStrategy commitStrategy) {
  auto icebergInsertHandle = checkedPointerCast<const IcebergInsertTableHandle>(
      connectorInsertTableHandle);

  return std::make_unique<IcebergDataSink>(
      inputType,
      icebergInsertHandle,
      connectorQueryCtx,
      commitStrategy,
      hiveConfig_,
      icebergConfig_);
}

} // namespace facebook::velox::connector::hive::iceberg
