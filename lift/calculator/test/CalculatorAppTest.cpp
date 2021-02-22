/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <filesystem>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "folly/Format.h"
#include "folly/Random.h"

#include "../../../pcf/io/FileManagerUtil.h"
#include "../../../pcf/mpc/EmpGame.h"
#include "../../common/Csv.h"
#include "../CalculatorApp.h"
#include "common/GenFakeData.h"
#include "common/LiftCalculator.h"

constexpr int32_t tsOffset = 10;

DEFINE_bool(is_conversion_lift, true, "is conversion lift");
DEFINE_int32(num_conversions_per_user, 4, "num of converstions per user");
DEFINE_int64(epoch, 1546300800, "epoch");

namespace private_lift {
class CalculatorAppTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = 5000 + folly::Random::rand32() % 1000;
    baseDir_ = "./";
    inputPathAlice_ = folly::sformat(
        "{}_input_alice_{}.csv", baseDir_, folly::Random::secureRand64());
    inputPathBob_ = folly::sformat(
        "{}_input_bob_{}.csv", baseDir_, folly::Random::secureRand64());
    outputPathAlice_ = folly::sformat(
        "{}_res_alice_{}", baseDir_, folly::Random::secureRand64());
    outputPathBob_ = folly::sformat(
        "{}_res_bob_{}", baseDir_, folly::Random::secureRand64());

    GenFakeData testDataGenerator;
    testDataGenerator.genFakePublisherInputFile(
        inputPathAlice_,
        15 /* numRows*/,
        0.5 /* opportunityRate */,
        0.5 /* testRate */,
        0.5 /* purchaseRate */,
        0.0 /* incrementalityRate */,
        1546300800 /* epoch */);
    testDataGenerator.genFakePartnerInputFile(
        inputPathBob_,
        15,
        0.5,
        0.5,
        0.5,
        0.0,
        1546300800,
        4 /* numConversionsPerRow */,
        false /* omitValuesColumn */);
  }

  void TearDown() override {
    std::filesystem::remove(outputPathAlice_);
    std::filesystem::remove(outputPathBob_);
    std::filesystem::remove(inputPathAlice_);
    std::filesystem::remove(inputPathBob_);
  }

  static void runGame(
      const pcf::Party party,
      const std::string& serverIp,
      const uint16_t port,
      const std::filesystem::path& inputPath,
      const std::string& outputPath,
      const bool useXorEncryption) {
    CalculatorApp(
        party, serverIp, port, inputPath, outputPath, useXorEncryption)
        .run();
  }

 protected:
  uint16_t port_;
  std::string baseDir_;
  std::string inputPathAlice_;
  std::string inputPathBob_;
  std::string outputPathAlice_;
  std::string outputPathBob_;
};

TEST_F(CalculatorAppTest, RandomInputTestVisibilityPublic) {
  auto futureAlice = std::async(
      runGame,
      pcf::Party::Alice,
      "",
      port_,
      inputPathAlice_,
      outputPathAlice_,
      false /* useXorEncryption */);
  auto futureBob = std::async(
      runGame,
      pcf::Party::Bob,
      "127.0.0.1",
      port_,
      inputPathBob_,
      outputPathBob_,
      false /* useXorEncryption */);

  futureAlice.wait();
  futureBob.wait();

  LiftCalculator liftCalculator;
  std::ifstream inFileAlice{inputPathAlice_};
  std::ifstream inFileBob{inputPathBob_};
  std::string linePublisher;
  std::string linePartner;
  getline(inFileAlice, linePublisher);
  getline(inFileBob, linePartner);
  auto headerPublisher = csv::splitByComma(linePublisher, false);
  auto headerPartner = csv::splitByComma(linePartner, false);
  auto colNameToIndex =
      liftCalculator.mapColToIndex(headerPublisher, headerPartner);
  OutputMetricsData computedResult =
      liftCalculator.compute(inFileAlice, inFileBob, colNameToIndex, tsOffset);
  GroupedLiftMetrics expectedRes;
  expectedRes.metrics = computedResult.toLiftMetrics();

  auto resAlice = GroupedLiftMetrics::fromJson(pcf::io::read(outputPathAlice_));
  auto resBob = GroupedLiftMetrics::fromJson(pcf::io::read(outputPathBob_));
  EXPECT_EQ(expectedRes, resAlice);
  EXPECT_EQ(expectedRes, resBob);
}
} // namespace private_lift