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

#include "velox/connectors/hive/iceberg/IcebergDataSource.h"

#include "velox/connectors/hive/iceberg/IcebergColumnHandle.h"
#include "velox/connectors/hive/iceberg/IcebergSplit.h"
#include "velox/connectors/hive/iceberg/IcebergSplitReader.h"

namespace facebook::velox::connector::hive::iceberg {

namespace {
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

// Helper function to extract data column assignments from changelog assignments.
// For changelog tables, the assignments map contains a 'rowdata' column handle
// whose ParquetFieldId.children contains the field IDs for all data columns.
// This function creates new IcebergColumnHandle instances for each data column
// using the field IDs from the rowdata column's children.
ColumnHandleMap extractDataColumnAssignments(
    const ColumnHandleMap& changelogAssignments,
    const RowTypePtr& dataSchema) {
  // Check if this is a changelog schema
  auto rowdataIt = changelogAssignments.find("rowdata");
  if (rowdataIt == changelogAssignments.end()) {
    // Not a changelog schema - return original assignments
    return changelogAssignments;
  }

  // Get the rowdata column handle
  auto rowdataHandle = std::dynamic_pointer_cast<const IcebergColumnHandle>(
      rowdataIt->second);
  VELOX_CHECK_NOT_NULL(
      rowdataHandle,
      "rowdata column handle must be an IcebergColumnHandle");

  // Get the field IDs from rowdata's children
  const auto& rowdataFieldId = rowdataHandle->field();
  const auto& childFieldIds = rowdataFieldId.children;

  VELOX_CHECK_EQ(
      childFieldIds.size(),
      dataSchema->size(),
      "Number of field IDs in rowdata must match number of data columns");

  // Create new column handles for each data column
  ColumnHandleMap dataAssignments;
  for (size_t i = 0; i < dataSchema->size(); ++i) {
    const auto& columnName = dataSchema->nameOf(i);
    const auto& columnType = dataSchema->childAt(i);

    auto dataColumnHandle = std::make_shared<IcebergColumnHandle>(
        columnName,
        HiveColumnHandle::ColumnType::kRegular,
        columnType,
        childFieldIds[i]);

    dataAssignments[columnName] = dataColumnHandle;
  }

  return dataAssignments;
}
} // namespace

IcebergDataSource::IcebergDataSource(
    const RowTypePtr& outputType,
    const ConnectorTableHandlePtr& tableHandle,
    const ColumnHandleMap& assignments,
    FileHandleFactory* fileHandleFactory,
    folly::Executor* ioExecutor,
    const ConnectorQueryCtx* connectorQueryCtx,
    const std::shared_ptr<HiveConfig>& hiveConfig,
    const RowTypePtr& changelogOutputType,
    const ColumnHandleMap& changeLogAssignments)
    : HiveDataSource(
          outputType,
          tableHandle,
          assignments,
          fileHandleFactory,
          ioExecutor,
          connectorQueryCtx,
          hiveConfig),
      columnHandles_(std::make_shared<ColumnHandleMap>(assignments)),
      changeLogAssignments_(std::make_shared<ColumnHandleMap>(changeLogAssignments)),
      changelogOutputType_(changelogOutputType) {}

std::unique_ptr<FileSplitReader> IcebergDataSource::createSplitReader() {
  auto icebergSplit = checkedPointerCast<const HiveIcebergSplit>(split_);

  // For changelog splits, use the changelog output type which should have the changelog schema
  auto outputType = icebergSplit->changelogSplitInfo ? changelogOutputType_ : readerOutputType_;

  auto reader = std::make_unique<IcebergSplitReader>(
      icebergSplit,
      tableHandle_,
      &partitionKeys_,
      connectorQueryCtx_,
      fileConfig_,
      outputType,
      dataIoStats_,
      metadataIoStats_,
      ioStats_,
      fileHandleFactory_,
      ioExecutor_,
      scanSpec_,
      columnHandles_);

  return reader;
}

} // namespace facebook::velox::connector::hive::iceberg

// Made with Bob
