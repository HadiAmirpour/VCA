#pragma once

#include "Common.h"

namespace vca {

void computeWeightedDCTEnergy(const Job &job, Result &result, unsigned blockSize);
void computeTextureSAD(Result &results, const Result &resultsPreviousFrame);

} // namespace vca
