/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-09-15
 *      Author: fasiondog
 */

#include "../../test_config.h"
#include <hikyuu/StockManager.h>
#include <hikyuu/trade_sys/system/crt/SYS_Simple.h>
#include <hikyuu/trade_sys/selector/crt/SE_Optimal.h>
#include <hikyuu/trade_sys/selector/imp/OptimalSelector.h>
#include <hikyuu/indicator/crt/MA.h>
#include <hikyuu/indicator/crt/KDATA.h>
#include <hikyuu/indicator/crt/NOT.h>
#include <hikyuu/trade_manage/crt/crtTM.h>
#include <hikyuu/trade_sys/signal/crt/SG_Bool.h>
#include <hikyuu/trade_sys/moneymanager/crt/MM_Nothing.h>

using namespace hku;

/**
 * @defgroup test_Selector_optimal test_Selector_optimal
 * @ingroup test_hikyuu_trade_sys_suite
 * @{
 */

static SYSPtr create_test_sys(int fast_n, int slow_n) {
    auto ind = MA(CLOSE(), fast_n) > MA(CLOSE(), slow_n);
    auto sg = SG_Bool(ind, NOT(ind));
    auto tm = crtTM();
    auto sys = SYS_Simple(tm, MM_Nothing(), EnvironmentPtr(), ConditionPtr(), sg);
    sys->name(fmt::format("test_sys_{}_{}", fast_n, slow_n));
    return sys;
}

/** @par 检测点 */
TEST_CASE("test_SE_Optimal") {
    auto se = SE_Optimal();
    CHECK_EQ(se->name(), "SE_Optimal");

    /** @arg 尝试加入空系统 */
    CHECK_THROWS(se->addSystem(SYSPtr()));
    CHECK_UNARY(se->getProtoSystemList().empty());

    /** @arg 尝试加入含有空系统的系统列表 */
    auto sys = create_test_sys(2, 3);
    sys->setStock(getStock("sz000001"));
    se->addSystemList(SystemList{sys});
    CHECK_EQ(se->getProtoSystemList().size(), 1);
    CHECK_THROWS(se->addSystemList(SystemList{sys, SYSPtr()}));
    CHECK_EQ(se->getProtoSystemList().size(), 2);

    /** @arg 尝试加入未指定证券标的的系统 */
    sys = create_test_sys(2, 3);
    // CHECK_THROWS(se->addSystem(sys));
    se->addSystem(sys);
    se->calculate(SystemList(), KQueryByIndex(-50));
    OptimalSelector* raw_se = dynamic_cast<OptimalSelector*>(se.get());
    CHECK_UNARY(raw_se->getRunRanges().empty());

    /** @arg 尝试加入未指定证券标的的系统列表 */
    sys = create_test_sys(2, 3);
    se->addSystemList({sys});
    se->calculate(SystemList(), KQueryByIndex(-50));
    CHECK_UNARY(raw_se->getRunRanges().empty());

    /** @arg 候选系统列表长度为0 */
    se->removeAll();
    REQUIRE(se->getProtoSystemList().empty());
    se->calculate(SystemList(), KQueryByIndex(-50));

    /** @arg 只有一个候选系统 */
    Stock stk = getStock("sz000001");
    sys = create_test_sys(2, 3);
    sys->setStock(stk);
    se->removeAll();
    se->addSystem(sys);
    REQUIRE(se->getProtoSystemList().size() == 1);
    KQuery query(-50);
    se->setParam<int>("train_len", 30);
    se->setParam<int>("test_len", 25);
    se->calculate(SystemList(), query);
    auto run_ranges = raw_se->getRunRanges();
    CHECK_EQ(run_ranges.size(), 1);
    auto dates = StockManager::instance().getTradingCalendar(query);
    CHECK_EQ(run_ranges[0].first, dates[30]);
    CHECK_EQ(run_ranges[0].second, dates[49] + Seconds(1));
    for (size_t i = 0; i < 30; i++) {
        CHECK_UNARY(se->getSelected(dates[i]).empty());
    }
    for (size_t i = 30; i < 50; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), sys->name());
    }

    query = KQueryByIndex(-60);
    se->setParam<int>("train_len", 20);
    se->setParam<int>("test_len", 10);
    se->reset();
    se->calculate(SystemList(), query);
    run_ranges = raw_se->getRunRanges();
    CHECK_EQ(run_ranges.size(), 4);
    dates = StockManager::instance().getTradingCalendar(query);
    CHECK_EQ(run_ranges[0].first, dates[20]);
    CHECK_EQ(run_ranges[0].second, dates[30]);
    CHECK_EQ(run_ranges[1].first, dates[30]);
    CHECK_EQ(run_ranges[1].second, dates[40]);
    CHECK_EQ(run_ranges[2].first, dates[40]);
    CHECK_EQ(run_ranges[2].second, dates[50]);
    CHECK_EQ(run_ranges[3].first, dates[50]);
    CHECK_EQ(run_ranges[3].second, dates[59] + Seconds(1));
    for (size_t i = 0; i < 20; i++) {
        CHECK_UNARY(se->getSelected(dates[i]).empty());
    }
    for (size_t i = 20; i < 60; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), sys->name());
    }

    /** @arg 多候选系统，取最大值 */
    se->removeAll();
    vector<std::pair<int, int>> params{{3, 5}, {3, 10}, {5, 10}, {5, 20}};
    for (const auto& param : params) {
        sys = create_test_sys(param.first, param.second);
        sys->setStock(stk);
        // sys->setParam("trace", true);
        se->addSystem(sys);
    }
    REQUIRE(se->getProtoSystemList().size() == params.size());

    query = KQueryByIndex(-125);
    // se->setParam<bool>("trace", true);
    // se->setParam<bool>("parallel", true);
    se->setParam<int>("train_len", 30);
    se->setParam<int>("test_len", 20);
    se->calculate(SystemList(), query);
    run_ranges = raw_se->getRunRanges();
    CHECK_EQ(run_ranges.size(), 5);
    dates = StockManager::instance().getTradingCalendar(query);
    CHECK_EQ(run_ranges[0].first, dates[30]);
    CHECK_EQ(run_ranges[0].second, dates[50]);
    CHECK_EQ(run_ranges[1].first, dates[50]);
    CHECK_EQ(run_ranges[1].second, dates[70]);
    CHECK_EQ(run_ranges[2].first, dates[70]);
    CHECK_EQ(run_ranges[2].second, dates[90]);
    CHECK_EQ(run_ranges[3].first, dates[90]);
    CHECK_EQ(run_ranges[3].second, dates[110]);
    CHECK_EQ(run_ranges[4].first, dates[110]);
    CHECK_EQ(run_ranges[4].second, dates[124] + Seconds(1));
    for (size_t i = 0; i < 30; i++) {
        CHECK_UNARY(se->getSelected(dates[i]).empty());
    }
    for (size_t i = 30; i < 50; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_3_10");
    }
    for (size_t i = 50; i < 125; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_3_5");
    }

    /** @arg 多候选系统，取最小值 */
    // se->setParam<bool>("trace", true);
    se->setParam<int>("mode", 1);
    se->reset();
    se->calculate(SystemList(), query);
    run_ranges = raw_se->getRunRanges();
    CHECK_EQ(run_ranges.size(), 5);
    dates = StockManager::instance().getTradingCalendar(query);
    CHECK_EQ(run_ranges[0].first, dates[30]);
    CHECK_EQ(run_ranges[0].second, dates[50]);
    CHECK_EQ(run_ranges[1].first, dates[50]);
    CHECK_EQ(run_ranges[1].second, dates[70]);
    CHECK_EQ(run_ranges[2].first, dates[70]);
    CHECK_EQ(run_ranges[2].second, dates[90]);
    CHECK_EQ(run_ranges[3].first, dates[90]);
    CHECK_EQ(run_ranges[3].second, dates[110]);
    CHECK_EQ(run_ranges[4].first, dates[110]);
    CHECK_EQ(run_ranges[4].second, dates[124] + Seconds(1));
    for (size_t i = 0; i < 30; i++) {
        CHECK_UNARY(se->getSelected(dates[i]).empty());
    }
    for (size_t i = 30; i < 50; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_5_10");
    }
    for (size_t i = 50; i < 70; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_5_20");
    }
    for (size_t i = 70; i < 110; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_5_10");
    }
    for (size_t i = 110; i < 125; i++) {
        auto sw = se->getSelected(dates[i]);
        CHECK_EQ(sw.size(), 1);
        CHECK_EQ(sw[0].sys->name(), "test_sys_5_20");
    }
}

//-----------------------------------------------------------------------------
// test export
//-----------------------------------------------------------------------------
#if HKU_SUPPORT_SERIALIZATION

/** @par 检测点 */
TEST_CASE("test_SE_Optimal_export") {
    StockManager& sm = StockManager::instance();
    string filename(sm.tmpdir());
    filename += "/SE_Optimal.xml";

    auto se1 = SE_Optimal();
    Stock stk = getStock("sz000001");
    vector<std::pair<int, int>> params{{3, 5}, {3, 10}, {5, 10}, {5, 20}};
    for (const auto& param : params) {
        auto sys = create_test_sys(param.first, param.second);
        sys->setStock(stk);
        // sys->setParam("trace", true);
        se1->addSystem(sys);
    }
    KQuery query(-125);
    se1->calculate(SystemList(), query);
    OptimalSelector* raw_se1 = dynamic_cast<OptimalSelector*>(se1.get());
    auto run_ranges1 = raw_se1->getRunRanges();

    // 目前计算后必须reset才能正常序列化后重加载
    se1->reset();

    {
        std::ofstream ofs(filename);
        boost::archive::xml_oarchive oa(ofs);
        oa << BOOST_SERIALIZATION_NVP(se1);
    }

    SEPtr se2;
    {
        std::ifstream ifs(filename);
        boost::archive::xml_iarchive ia(ifs);
        ia >> BOOST_SERIALIZATION_NVP(se2);
    }

    CHECK_EQ(se1->name(), se2->name());
    CHECK_EQ(se1->getProtoSystemList().size(), se2->getProtoSystemList().size());

    // se2->setParam<bool>("trace", true);
    se2->calculate(SystemList(), query);
    OptimalSelector* raw_se2 = dynamic_cast<OptimalSelector*>(se2.get());
    auto run_ranges2 = raw_se2->getRunRanges();
    CHECK_EQ(run_ranges1.size(), run_ranges2.size());
    for (size_t i = 0, len = run_ranges1.size(); i < len; i++) {
        CHECK_EQ(run_ranges1[i], run_ranges2[i]);
    }

    auto dates = StockManager::instance().getTradingCalendar(query);
    se1->calculate(SystemList(), query);
    for (const auto& date : dates) {
        if (se1->getSelected(date).empty()) {
            CHECK_UNARY(se2->getSelected(date).empty());
        } else {
            CHECK_EQ(se1->getSelected(date)[0].sys->name(), se2->getSelected(date)[0].sys->name());
        }
    }
}
#endif /* #if HKU_SUPPORT_SERIALIZATION */

/** @} */