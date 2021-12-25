#include "vcaLib.h"
#include "analyzer.h"

#include "analyzer/Analyzer.h"

DLL_PUBLIC vca_analyzer *vca_analyzer_open(vca_param param)
{
    return new vca::Analyzer(param);
}

DLL_PUBLIC push_result vca_analyzer_push(vca_analyzer *enc, vca_frame *frame)
{
    if (enc == nullptr)
        return push_result::VCA_PUSH_ERROR;

    auto analyzer = (vca::Analyzer *) enc;
    if (analyzer == nullptr)
        return push_result::VCA_PUSH_ERROR;

    return analyzer->pushFrame(frame);
}

DLL_PUBLIC vca_frame_results vca_analyzer_pull_frame_result(vca_analyzer *enc)
{
    auto analyzer = (vca::Analyzer*)(enc);
    return analyzer->pullResult();
}

DLL_PUBLIC void vca_analyzer_close(vca_analyzer *enc) 
{
    auto analyzer = (vca::Analyzer *) enc;
    delete analyzer;
}

DLL_PUBLIC void vca_analyzer_shot_detect(vca_analyzer *enc) {}