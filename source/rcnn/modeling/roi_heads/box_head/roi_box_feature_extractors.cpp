#include "roi_heads/box_head/roi_box_feature_extractors.h"
#include "make_layers.h"
#include "defaults.h"
#include <cmath>


namespace rcnn{
namespace modeling{

ResNet50Conv5ROIFeatureExtractorImpl::ResNet50Conv5ROIFeatureExtractorImpl(int64_t in_channels)
  :pooler_(MakePooler("ROI_BOX_HEAD")),
  head_(
    ResNetHead(
      std::vector<ResNetImpl::StageSpec>{ResNetImpl::StageSpec(4, 3, false)},
      rcnn::config::GetCFG<int64_t>({"MODEL", "RESNETS", "NUM_GROUPS"}),
      rcnn::config::GetCFG<int64_t>({"MODEL", "RESNETS", "WIDTH_PER_GROUP"}),
      rcnn::config::GetCFG<bool>({"MODEL", "RESNETS", "STRIDE_IN_1X1"}),
      0,
      rcnn::config::GetCFG<int64_t>({"MODEL", "RESNETS", "RES2_OUT_CHANNELS"}),
      rcnn::config::GetCFG<int64_t>({"MODEL", "RESNETS", "RES5_DILATION"})
    )
  ),
  out_channels(head_->out_channels_){}

torch::Tensor ResNet50Conv5ROIFeatureExtractorImpl::forward(std::vector<torch::Tensor> x, std::vector<rcnn::structures::BoxList> proposals){
  torch::Tensor output = pooler_(x, proposals);
  output = head_(output);
  return output;
}

FPN2MLPFeatureExtractorImpl::FPN2MLPFeatureExtractorImpl(int64_t in_channels)
  :pooler_(MakePooler("ROI_BOX_HEAD")),
  fc6_(layers::MakeFC(
      pow(in_channels * rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "POOLER_RESOLUTION"}), 2),
      rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"})
    )),//fc6
  fc7_(layers::MakeFC(
      rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"}),
      rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"})
    )),//fc7
  out_channels(rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"})){}

torch::Tensor FPN2MLPFeatureExtractorImpl::forward(std::vector<torch::Tensor> x, std::vector<rcnn::structures::BoxList> proposals){
  torch::Tensor output = pooler_(x, proposals);
  output = output.reshape({output.size(0), -1});
  output = fc6_(output).relu_();
  output = fc7_(output).relu_();
  return output;
}

torch::nn::Sequential FPNXconv1fcFeatureExtractorImpl::make_xconvs(int64_t in_channels){
  torch::nn::Sequential xconvs;
  int64_t conv_head_dim = rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "CONV_HEAD_DIM"});
  int64_t num_stacked_convs = rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "NUM_STACKED_CONVS"});
  int64_t dilation = rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "DILATION"});
  for(size_t i = 0; i < num_stacked_convs; ++i){
    xconvs->push_back(
      torch::nn::Conv2d(
        torch::nn::Conv2dOptions(in_channels, conv_head_dim, 3).stride(1).padding(dilation).dilation(dilation).with_bias(true)
      )
    );
    in_channels = conv_head_dim;
    xconvs->push_back(torch::nn::Functional(torch::relu));
  }
  for(auto& param : xconvs->named_parameters()){
    if(param.key().find("weight") != std::string::npos) {
      torch::nn::init::normal_(param.value(), 0.01);
    }
    else if(param.key().find("bias") != std::string::npos) {
      torch::nn::init::constant_(param.value(), 0);
    }
  }
  return xconvs;
}

FPNXconv1fcFeatureExtractorImpl::FPNXconv1fcFeatureExtractorImpl(int64_t in_channels)
  :pooler_(MakePooler("ROI_BOX_HEAD")),
  xconvs_(make_xconvs(in_channels)),
  fc6_(layers::MakeFC(
      pow(rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "CONV_HEAD_DIM"}) * rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "POOLER_RESOLUTION"}), 2),
      rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"})
    )),
  out_channels(rcnn::config::GetCFG<int64_t>({"MODEL", "ROI_BOX_HEAD", "MLP_HEAD_DIM"})){}

torch::Tensor FPNXconv1fcFeatureExtractorImpl::forward(std::vector<torch::Tensor> x, std::vector<rcnn::structures::BoxList> proposals){
  torch::Tensor output = pooler_(x, proposals);
  output = xconvs_->forward(output);
  output = output.reshape({output.size(0), -1});
  output = fc6_(output).relu_();
  return output;
}

torch::nn::Sequential MakeROIBoxFeatureExtractor(int64_t in_channels){
  torch::nn::Sequential extractor;
  rcnn::config::CFGS name = rcnn::config::GetCFG<rcnn::config::CFGS>({"MODEL", "ROI_BOX_HEAD", "FEATURE_EXTRACTOR"});
  std::string extractor_name = name.get();
  if(extractor_name.compare("ResNet50Conv5ROIFeatureExtractor") == 0){
    extractor->push_back(ResNet50Conv5ROIFeatureExtractor(in_channels));
  }
  else if(extractor_name.compare("FPN2MLPFeatureExtractor") == 0){
    extractor->push_back(FPN2MLPFeatureExtractor(in_channels));
  }
  else if(extractor_name.compare("FPNXconv1fcFeatureExtractor") == 0){
    extractor->push_back(FPNXconv1fcFeatureExtractor(in_channels));
  }
  else{
    assert(false);
  }
  return extractor;
}

}
}