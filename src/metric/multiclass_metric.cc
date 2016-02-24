/*!
 * Copyright 2015 by Contributors
 * \file multiclass_metric.cc
 * \brief evaluation metrics for multiclass classification.
 * \author Kailong Chen, Tianqi Chen
 */
#include <xgboost/metric.h>
#include <cmath>
#include "../common/sync.h"
#include "../common/math.h"

namespace xgboost {
namespace metric {
// tag the this file, used by force static link later.
DMLC_REGISTRY_FILE_TAG(multiclass_metric);

/*!
 * \brief base class of multi-class evaluation
 * \tparam Derived the name of subclass
 */
template<typename Derived>
struct EvalMClassBase : public Metric {
  float Eval(const std::vector<float> &preds,
             const MetaInfo &info,
             bool distributed) const override {
    CHECK_NE(info.labels.size(), 0) << "label set cannot be empty";
    CHECK(preds.size() % info.labels.size() == 0)
        << "label and prediction size not match";
    const size_t nclass = preds.size() / info.labels.size();
    CHECK_GE(nclass, 1)
        << "mlogloss and merror are only used for multi-class classification,"
        << " use logloss for binary classification";
    const bst_omp_uint ndata = static_cast<bst_omp_uint>(info.labels.size());
    double sum = 0.0, wsum = 0.0;
    int label_error = 0;
    #pragma omp parallel for reduction(+: sum, wsum) schedule(static)
    for (bst_omp_uint i = 0; i < ndata; ++i) {
      const float wt = info.GetWeight(i);
      int label =  static_cast<int>(info.labels[i]);
      if (label >= 0 && label < static_cast<int>(nclass)) {
        sum += Derived::EvalRow(label,
                                dmlc::BeginPtr(preds) + i * nclass,
                                nclass) * wt;
        wsum += wt;
      } else {
        label_error = label;
      }
    }
    CHECK(label_error >= 0 && label_error < static_cast<int>(nclass))
        << "MultiClassEvaluation: label must be in [0, num_class),"
        << " num_class=" << nclass << " but found " << label_error << " in label";

    double dat[2]; dat[0] = sum, dat[1] = wsum;
    if (distributed) {
      rabit::Allreduce<rabit::op::Sum>(dat, 2);
    }
    return Derived::GetFinal(dat[0], dat[1]);
  }
  /*!
   * \brief to be implemented by subclass,
   *   get evaluation result from one row
   * \param label label of current instance
   * \param pred prediction value of current instance
   * \param nclass number of class in the prediction
   */
  inline static float EvalRow(int label,
                              const float *pred,
                              size_t nclass);
  /*!
   * \brief to be overridden by subclass, final transformation
   * \param esum the sum statistics returned by EvalRow
   * \param wsum sum of weight
   */
  inline static float GetFinal(float esum, float wsum) {
    return esum / wsum;
  }
  // used to store error message
  const char *error_msg_;
};

/*! \brief match error */
struct EvalMatchError : public EvalMClassBase<EvalMatchError> {
  const char* Name() const override {
    return "merror";
  }
  inline static float EvalRow(int label,
                              const float *pred,
                              size_t nclass) {
    return common::FindMaxIndex(pred, pred + nclass) != pred + static_cast<int>(label);
  }
};

/*! \brief match error */
struct EvalMultiLogLoss : public EvalMClassBase<EvalMultiLogLoss> {
  const char* Name() const override {
    return "mlogloss";
  }
  inline static float EvalRow(int label,
                              const float *pred,
                              size_t nclass) {
    const float eps = 1e-16f;
    size_t k = static_cast<size_t>(label);
    if (pred[k] > eps) {
      return -std::log(pred[k]);
    } else {
      return -std::log(eps);
    }
  }
};

XGBOOST_REGISTER_METRIC(MatchError, "merror")
.describe("Multiclass classification error.")
.set_body([](const char* param) { return new EvalMatchError(); });

XGBOOST_REGISTER_METRIC(MultiLogLoss, "mlogloss")
.describe("Multiclass negative loglikelihood.")
.set_body([](const char* param) { return new EvalMultiLogLoss(); });
}  // namespace metric
}  // namespace xgboost