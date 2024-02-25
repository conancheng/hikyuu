/*
 * IStdp.cpp
 *
 *  Copyright (c) 2019 hikyuu.org
 *
 *  Created on: 2019-4-18
 *      Author: fasiondog
 */

#include "IStdp.h"
#include "../crt/MA.h"

#if HKU_SUPPORT_SERIALIZATION
BOOST_CLASS_EXPORT(hku::IStdp)
#endif

namespace hku {

IStdp::IStdp() : IndicatorImp("STDP", 1) {
    setParam<int>("n", 10);
}

IStdp::~IStdp() {}

bool IStdp::check() {
    return getParam<int>("n") >= 2;
}

void IStdp::_calculate(const Indicator& data) {
    size_t total = data.size();
    m_discard = data.discard();
    if (m_discard >= total) {
        m_discard = total;
        return;
    }

    int n = getParam<int>("n");

    auto const* src = data.data();
    auto* dst = this->data();

    vector<price_t> pow_buf(data.size());
    price_t ex = 0.0, ex2 = 0.0;
    size_t num = 0;
    size_t start_pos = m_discard;
    size_t first_end = start_pos + n >= total ? total : start_pos + n;
    price_t k = src[start_pos];
    for (size_t i = start_pos; i < first_end; i++) {
        num++;
        price_t d = src[i] - k;
        ex += d;
        price_t d_pow = std::pow(d, 2);
        pow_buf[i] = d_pow;
        ex2 += d_pow;
        dst[i] = std::sqrt((ex2 - std::pow(ex, 2) / num) / num);
    }

    for (size_t i = first_end; i < total; i++) {
        ex -= src[i - n] - k;
        ex2 -= pow_buf[i - n];
        price_t d = src[i] - k;
        ex += d;
        price_t d_pow = std::pow(d, 2);
        pow_buf[i] = d_pow;
        ex2 += d_pow;
        dst[i] = std::sqrt((ex2 - std::pow(ex, 2) / n) / n);
    }
}

void IStdp::_dyn_run_one_step(const Indicator& ind, size_t curPos, size_t step) {
    size_t start = _get_step_start(curPos, step, ind.discard());
    size_t num = 0;
    price_t ex = 0.0, ex2 = 0.0;
    price_t k = ind[start];
    for (size_t i = start; i <= curPos; i++) {
        num++;
        price_t d = ind[i] - k;
        ex += d;
        ex2 += std::pow(d, 2);
    }
    _set(num == 0 ? 0.0 : std::sqrt((ex2 - std::pow(ex, 2) / num) / num), curPos);
}

Indicator HKU_API STDP(int n) {
    IndicatorImpPtr p = make_shared<IStdp>();
    p->setParam<int>("n", n);
    return Indicator(p);
}

Indicator HKU_API STDP(const IndParam& n) {
    IndicatorImpPtr p = make_shared<IStdp>();
    p->setIndParam("n", n);
    return Indicator(p);
}

} /* namespace hku */
