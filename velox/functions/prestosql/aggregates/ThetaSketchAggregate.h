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

#include "velox/exec/Aggregate.h"
#include "velox/exec/SimpleAggregateAdapter.h"
#include "velox/external/theta/ThetaSketch.h"
#include "velox/external/theta/ThetaUnion.h"
#include "velox/functions/prestosql/aggregates/AggregateNames.h"

using namespace facebook::velox::exec;
using namespace facebook::velox::common::theta;

namespace facebook::velox::aggregate {

namespace {

template <typename T>
class ThetaSketchAggregate {
 public:
  // Type(s) of input vector(s) wrapped in Row.
  using InputType = Row<T>;

  // Type of intermediate result
  using IntermediateType = Varbinary;

  // Type of output vector.
  using OutputType = Varbinary;

  static bool toIntermediate(
      exec::out_type<IntermediateType>& out,
      exec::arg_type<T> in) {
    auto updateSketch = updateThetaSketch::builder().build();
    updateSketch.update(in);
    auto compactSketch = updateSketch.compact();
    out.resize(compactSketch.getSerializedSizeBytes());
    compactSketch.serialize(out.data());
    return true;
  }

  struct AccumulatorType {
    ThetaUnion thetaUnion = ThetaUnion::builder().build();
    updateThetaSketch updateSketch = updateThetaSketch::builder().build();

    AccumulatorType() = delete;

    // Constructor used in initializeNewGroups().
    explicit AccumulatorType(
        HashStringAllocator* /*allocator*/,
        ThetaSketchAggregate* /*fn*/) {}

    // addInput expects one parameter of exec::arg_type<T> for each child-type T
    // wrapped in InputType.
    void addInput(HashStringAllocator* /*allocator*/, exec::arg_type<T> data) {
      updateSketch.update(data);
    }

    // combine expects one parameter of exec::arg_type<IntermediateType>.
    void combine(
        HashStringAllocator* /*allocator*/,
        exec::arg_type<IntermediateType> other) {
      thetaUnion.update(updateSketch);
      auto compactSketch =
          wrappedCompactThetaSketch::wrap(other.data(), other.size());
      thetaUnion.update(compactSketch);
      updateSketch.reset();
    }

    bool writeFinalResult(exec::out_type<Varbinary>& out) {
      thetaUnion.update(updateSketch);
      auto compactSketch = thetaUnion.getResult();
      out.resize(compactSketch.getSerializedSizeBytes());
      compactSketch.serialize(out.data());
      updateSketch.reset();
      return true;
    }

    bool writeIntermediateResult(exec::out_type<Varbinary>& out) {
      thetaUnion.update(updateSketch);
      auto compactSketch = thetaUnion.getResult();
      out.resize(compactSketch.getSerializedSizeBytes());
      compactSketch.serialize(out.data());
      updateSketch.reset();
      return true;
    }
  };
};

} // namespace

exec::AggregateRegistrationResult registerThetaSketchAggregate(
    const std::string& prefix,
    bool withCompanionFunctions,
    bool overwrite) {
  std::vector<std::shared_ptr<exec::AggregateFunctionSignature>> signatures;

  for (const auto& inputType :
       {"smallint", "integer", "bigint", "real", "double"}) {
    signatures.push_back(exec::AggregateFunctionSignatureBuilder()
                             .returnType("varbinary")
                             .intermediateType("varbinary")
                             .argumentType(inputType)
                             .build());
  }

  auto name = prefix + kThetaSketch;

  return exec::registerAggregateFunction(
      name,
      std::move(signatures),
      [name](
          core::AggregationNode::Step step,
          const std::vector<TypePtr>& argTypes,
          const TypePtr& resultType,
          const core::QueryConfig& /*config*/)
          -> std::unique_ptr<exec::Aggregate> {
        VELOX_CHECK_LE(
            argTypes.size(), 1, "{} takes at most one argument", name);
        auto inputType = argTypes[0];
        if (exec::isRawInput(step)) {
          switch (inputType->kind()) {
            case TypeKind::SMALLINT:
              return std::make_unique<
                  SimpleAggregateAdapter<ThetaSketchAggregate<int16_t>>>(
                  step, argTypes, resultType);
            case TypeKind::INTEGER:
              return std::make_unique<
                  SimpleAggregateAdapter<ThetaSketchAggregate<int32_t>>>(
                  step, argTypes, resultType);
            case TypeKind::BIGINT:
              return std::make_unique<
                  SimpleAggregateAdapter<ThetaSketchAggregate<int64_t>>>(
                  step, argTypes, resultType);
            case TypeKind::REAL:
              return std::make_unique<
                  SimpleAggregateAdapter<ThetaSketchAggregate<float>>>(
                  step, argTypes, resultType);
            case TypeKind::DOUBLE:
              return std::make_unique<
                  SimpleAggregateAdapter<ThetaSketchAggregate<double>>>(
                  step, argTypes, resultType);
            default:
              VELOX_FAIL(
                  "Unknown input type for {} aggregation {}",
                  name,
                  inputType->kindName());
          }
        } else {
          return std::make_unique<
              SimpleAggregateAdapter<ThetaSketchAggregate<Varbinary>>>(
              step, argTypes, resultType);
        }
      },
      withCompanionFunctions,
      overwrite);
}

} // namespace facebook::velox::aggregate
