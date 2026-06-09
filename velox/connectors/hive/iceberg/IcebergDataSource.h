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
#pragma once

#include "velox/connectors/hive/HiveDataSource.h"

namespace facebook::velox::connector::hive::iceberg {

/// Iceberg-specific data source that extends HiveDataSource.
///
/// Provides Iceberg table format support by creating
/// IcebergSplitReader instances that handle:
/// - Positional delete files for row-level deletes.
/// - Schema evolution with column adaptation.
/// - Iceberg-specific metadata columns.
class IcebergDataSource : public HiveDataSource {
 public:
  IcebergDataSource(
      const RowTypePtr& outputType,
      const ConnectorTableHandlePtr& tableHandle,
      const ColumnHandleMap& assignments,
      FileHandleFactory* fileHandleFactory,
      folly::Executor* ioExecutor,
      const ConnectorQueryCtx* connectorQueryCtx,
      const std::shared_ptr<HiveConfig>& hiveConfig,
      const RowTypePtr& changelogOutputType = nullptr,
      const ColumnHandleMap& changeLogAssignments = {});

 protected:

  /// Creates an IcebergSplitReader for reading Iceberg data files.
  ///
  /// Unlike the base HiveDataSource which creates a generic FileSplitReader,
  /// this method creates an IcebergSplitReader that handles Iceberg-specific
  /// features like positional delete files and schema evolution.
  std::unique_ptr<FileSplitReader> createSplitReader() override;

  const RowTypePtr getOutputType() override {
    return changelogOutputType_ ? changelogOutputType_ : HiveDataSource::getOutputType();
  }

 private:
  /// Column handles map for accessing column metadata.
  std::shared_ptr<ColumnHandleMap> columnHandles_;
  std::shared_ptr<ColumnHandleMap> changeLogAssignments_;
  
  /// For changelog splits, stores the original changelog output type
  /// (operation, ordinal, snapshotid, rowdata) while readerOutputType_
  /// is temporarily set to the data columns for scan spec creation.
  const RowTypePtr changelogOutputType_;
};

} // namespace facebook::velox::connector::hive::iceberg
