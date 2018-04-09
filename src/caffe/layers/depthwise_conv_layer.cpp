#include <vector>
#include "caffe/layers/depthwise_conv_layer.hpp"

namespace caffe {
  template <typename Dtype>
void DepthwiseConvolutionLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  ConvolutionParameter conv_param = this->layer_param_.convolution_param();
  if (conv_param.has_kernel_h() && conv_param.has_kernel_w()) {
    kernel_h_ = conv_param.kernel_h();
    kernel_w_ = conv_param.kernel_w();
  } else {
    if (conv_param.kernel_size_size() == 1)
    {
      kernel_h_ = conv_param.kernel_size(0);
      kernel_w_ = conv_param.kernel_size(0);
    }
    else
    {
      kernel_h_ = conv_param.kernel_size(0);
      kernel_w_ = conv_param.kernel_size(1);
    }
  }
  if (conv_param.has_stride_h() && conv_param.has_stride_w()) {
    stride_h_ = conv_param.stride_h();
    stride_w_ = conv_param.stride_w();
  } else {
    if (conv_param.stride_size() == 1)
    {
      stride_h_ = conv_param.stride(0);
      stride_w_ = conv_param.stride(0);
    }
    else
    {
      stride_h_ = conv_param.stride(0);
      stride_w_ = conv_param.stride(1);
    }
  }
  if (conv_param.has_pad_h() && conv_param.has_pad_w()) {
    pad_h_ = conv_param.pad_h();
    pad_w_ = conv_param.pad_w();
  } else {
    if (conv_param.pad_size() == 1)
    {
      pad_h_ = conv_param.pad(0);
      pad_w_ = conv_param.pad(0);
    }
    else
    {
      pad_h_ = conv_param.pad(0);
      pad_w_ = conv_param.pad(1);
    }
  }
  if (conv_param.dilation_size() > 0)
  {
    if (conv_param.dilation_size() == 1)
    {
      dilation_h_ = conv_param.dilation(0);
      dilation_w_ = conv_param.dilation(0);
    }
    else
    {
      dilation_h_ = conv_param.dilation(0);
      dilation_w_ = conv_param.dilation(1);
    }
  }
  else
  {
    dilation_h_ = 1;
    dilation_w_ = 1;
  }
  vector<int> weight_shape(4);
  weight_shape[0] = bottom[0]->channels();
  weight_shape[1] = 1;
  weight_shape[2] = kernel_h_;
  weight_shape[3] = kernel_w_;
  vector<int> bias_shape;
  if (conv_param.bias_term())
  {
    bias_shape.push_back(bottom[0]->channels());
  }
  if (this->blobs_.size() == 0) {
    if (conv_param.bias_term()) {
      this->blobs_.resize(2);
    } else {
      this->blobs_.resize(1);
    }
    this->blobs_[0].reset(new Blob<Dtype>(weight_shape));
    shared_ptr<Filler<Dtype> > weight_filler(GetFiller<Dtype>(conv_param.weight_filler()));
    weight_filler->Fill(this->blobs_[0].get());
    if (conv_param.bias_term()) {
      this->blobs_[1].reset(new Blob<Dtype>(bias_shape));
      shared_ptr<Filler<Dtype> > bias_filler(GetFiller<Dtype>(conv_param.bias_filler()));
      bias_filler->Fill(this->blobs_[1].get());
    }
  }
  this->param_propagate_down_.resize(this->blobs_.size(), true);
}

template <typename Dtype>
void DepthwiseConvolutionLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  vector<int> top_shape;
  top_shape.push_back(bottom[0]->num());
  top_shape.push_back(bottom[0]->channels());
  top_shape.push_back((bottom[0]->height() + 2 * pad_h_ - (dilation_h_ * (kernel_h_ - 1) + 1)) / stride_h_ + 1);
  top_shape.push_back((bottom[0]->width() + 2 * pad_w_ - (dilation_w_ * (kernel_w_ - 1) + 1)) / stride_w_ + 1);
  top[0]->Reshape(top_shape);
  vector<int> weight_buffer_shape;
  weight_buffer_shape.push_back(bottom[0]->channels());
  weight_buffer_shape.push_back(kernel_h_);
  weight_buffer_shape.push_back(kernel_w_);
  weight_buffer_shape.push_back(bottom[0]->num());
  weight_buffer_shape.push_back(top[0]->height());
  weight_buffer_shape.push_back(top[0]->width());
  weight_buffer_.Reshape(weight_buffer_shape);
  vector<int> weight_multiplier_shape;
  weight_multiplier_shape.push_back(bottom[0]->num());
  weight_multiplier_shape.push_back(top[0]->height());
  weight_multiplier_shape.push_back(top[0]->width());
  weight_multiplier_.Reshape(weight_multiplier_shape);
  caffe_gpu_set(weight_multiplier_.count(), Dtype(1), weight_multiplier_.mutable_gpu_data());
  if (this->layer_param_.convolution_param().bias_term())
  {
    vector<int> bias_buffer_shape;
    bias_buffer_shape.push_back(bottom[0]->channels());
    bias_buffer_shape.push_back(bottom[0]->num());
    bias_buffer_shape.push_back(top[0]->height());
    bias_buffer_shape.push_back(top[0]->width());
    bias_buffer_.Reshape(bias_buffer_shape);
    vector<int> bias_multiplier_shape;
    bias_multiplier_shape.push_back(bottom[0]->num());
    bias_multiplier_shape.push_back(top[0]->height());
    bias_multiplier_shape.push_back(top[0]->width());
    bias_multiplier_.Reshape(bias_multiplier_shape);
    caffe_gpu_set(bias_multiplier_.count(), Dtype(1), bias_multiplier_.mutable_gpu_data());
  }
}


template <typename Dtype>
void DepthwiseConvolutionLayer<Dtype>::compute_output_shape() {
  const int* kernel_shape_data = this->kernel_shape_.cpu_data();
  const int* stride_data = this->stride_.cpu_data();
  const int* pad_data = this->pad_.cpu_data();
  const int* dilation_data = this->dilation_.cpu_data();
  this->output_shape_.clear();
  for (int i = 0; i < this->num_spatial_axes_; ++i) {
    // i + 1 to skip channel axis
    const int input_dim = this->input_shape(i + 1);
    const int kernel_extent = dilation_data[i] * (kernel_shape_data[i] - 1) + 1;
    const int output_dim = (input_dim + 2 * pad_data[i] - kernel_extent)
        / stride_data[i] + 1;
    this->output_shape_.push_back(output_dim);
  }
}

template <typename Dtype>
void DepthwiseConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
	const Dtype* weight = this->blobs_[0]->cpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* top_data = top[i]->mutable_cpu_data();
    for (int n = 0; n < this->num_; ++n) {
      this->forward_cpu_gemm(bottom_data + n * this->bottom_dim_, weight,
          top_data + n * this->top_dim_);
      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->cpu_data();
        this->forward_cpu_bias(top_data + n * this->top_dim_, bias);
      }
    }
  }
}

template <typename Dtype>
void DepthwiseConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->cpu_diff();
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1]) {
      Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->backward_cpu_bias(bias_diff, top_diff + n * this->top_dim_);
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      for (int n = 0; n < this->num_; ++n) {
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0]) {
          this->weight_cpu_gemm(bottom_data + n * this->bottom_dim_,
              top_diff + n * this->top_dim_, weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_cpu_gemm(top_diff + n * this->top_dim_, weight,
              bottom_diff + n * this->bottom_dim_);
        }
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(DepthwiseConvolutionLayer);
#endif

INSTANTIATE_CLASS(DepthwiseConvolutionLayer);
REGISTER_LAYER_CLASS(DepthwiseConvolution);
}  // namespace caffe
