#include <fcntl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc.hpp>
#endif  // USE_OPENCV
#include <stdint.h>

#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <string>
#include <vector>

#include "caffe/common.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/io.hpp"

const int kProtoReadBytesLimit = INT_MAX;  // Max size of 2 GB minus 1 byte.

namespace caffe {

using google::protobuf::io::FileInputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::io::CodedInputStream;
using google::protobuf::io::ZeroCopyOutputStream;
using google::protobuf::io::CodedOutputStream;
using google::protobuf::Message;

bool ReadProtoFromTextFile(const char* filename, Message* proto) {
  int fd = open(filename, O_RDONLY);
  CHECK_NE(fd, -1) << "File not found: " << filename;
  FileInputStream* input = new FileInputStream(fd);
  bool success = google::protobuf::TextFormat::Parse(input, proto);
  delete input;
  close(fd);
  return success;
}

void WriteProtoToTextFile(const Message& proto, const char* filename) {
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  FileOutputStream* output = new FileOutputStream(fd);
  CHECK(google::protobuf::TextFormat::Print(proto, output));
  delete output;
  close(fd);
}

bool ReadProtoFromBinaryFile(const char* filename, Message* proto) {
  int fd = open(filename, O_RDONLY);
  CHECK_NE(fd, -1) << "File not found: " << filename;
  ZeroCopyInputStream* raw_input = new FileInputStream(fd);
  CodedInputStream* coded_input = new CodedInputStream(raw_input);
  coded_input->SetTotalBytesLimit(kProtoReadBytesLimit, 536870912);

  bool success = proto->ParseFromCodedStream(coded_input);

  delete coded_input;
  delete raw_input;
  close(fd);
  return success;
}

void WriteProtoToBinaryFile(const Message& proto, const char* filename) {
  fstream output(filename, ios::out | ios::trunc | ios::binary);
  CHECK(proto.SerializeToOstream(&output)) << "Possible reasons: no disk space, "
      "no write permissions, the destination folder doesn't exist";
}

#ifdef USE_OPENCV
cv::Mat ReadImageToCVMat(const string& filename,
    const int height, const int width, const bool is_color) {
  cv::Mat cv_img;
  int cv_read_flag = (is_color ? CV_LOAD_IMAGE_COLOR :
    CV_LOAD_IMAGE_GRAYSCALE);
  cv::Mat cv_img_origin = cv::imread(filename, cv_read_flag);
  if (!cv_img_origin.data) {
    LOG(ERROR) << "Could not open or find file " << filename;
    return cv_img_origin;
  }
  if (height > 0 && width > 0) {
    cv::resize(cv_img_origin, cv_img, cv::Size(width, height));
  } else {
    cv_img = cv_img_origin;
  }
  return cv_img;
}

cv::Mat ReadImageToCVMat(const string& filename,
    const int height, const int width) {
  return ReadImageToCVMat(filename, height, width, true);
}

cv::Mat ReadImageToCVMat(const string& filename,
    const bool is_color) {
  return ReadImageToCVMat(filename, 0, 0, is_color);
}

cv::Mat ReadImageToCVMat(const string& filename) {
  return ReadImageToCVMat(filename, 0, 0, true);
}

// Do the file extension and encoding match?
static bool matchExt(const std::string & fn,
                     std::string en) {
  size_t p = fn.rfind('.');
  std::string ext = p != fn.npos ? fn.substr(p) : fn;
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  std::transform(en.begin(), en.end(), en.begin(), ::tolower);
  if ( ext == en )
    return true;
  if ( en == "jpg" && ext == "jpeg" )
    return true;
  return false;
}

bool ReadImageToDatum(const string& filename, const int label,
    const int height, const int width, const bool is_color,
    const std::string & encoding, Datum* datum) {
  cv::Mat cv_img = ReadImageToCVMat(filename, height, width, is_color);
  if (cv_img.data) {
    if (encoding.size()) {
      if ( (cv_img.channels() == 3) == is_color && !height && !width &&
          matchExt(filename, encoding) ) {
        return ReadFileToDatum(filename, label, datum);
      }
      std::vector<uchar> buf;
      cv::imencode("."+encoding, cv_img, buf);
      datum->set_data(std::string(reinterpret_cast<char*>(&buf[0]),
                      buf.size()));
      datum->set_label(label);
      datum->set_encoded(true);
      return true;
    }
    CVMatToDatum(cv_img, *datum);
    datum->set_label(label);
    return true;
  } else {
    return false;
  }
}
#endif  // USE_OPENCV

bool ReadFileToDatum(const string& filename, const int label,
    Datum* datum) {
  std::streampos size;

  fstream file(filename.c_str(), ios::in|ios::binary|ios::ate);
  if (file.is_open()) {
    size = file.tellg();
    std::string buffer(size, ' ');
    file.seekg(0, ios::beg);
    file.read(&buffer[0], size);
    file.close();
    datum->set_data(buffer);
    datum->set_label(label);
    datum->set_encoded(true);
    return true;
  } else {
    return false;
  }
}

#ifdef USE_OPENCV
cv::Mat DecodeDatumToCVMatNative(const Datum& datum) {
  cv::Mat cv_img;
  DecodeDatumToCVMatNative(datum, cv_img);
  return cv_img;
}

void DecodeDatumToCVMatNative(const Datum& datum, cv::Mat& cv_img) {
  CHECK(datum.encoded()) << "Datum not encoded";
  const string& data = datum.data();
  std::vector<char> vec_data(data.c_str(), data.c_str() + data.size());
  cv_img = cv::imdecode(vec_data, -1);
  if (!cv_img.data) {
    LOG(ERROR) << "Could not decode datum ";
  }
}

cv::Mat DecodeDatumToCVMat(const Datum& datum, bool is_color) {
  cv::Mat cv_img;
  DecodeDatumToCVMat(datum, is_color, cv_img);
  return cv_img;
}

void DecodeDatumToCVMat(const Datum& datum, bool is_color, cv::Mat& cv_img) {
  CHECK(datum.encoded()) << "Datum not encoded";
  const string& data = datum.data();
  std::vector<char> vec_data(data.c_str(), data.c_str() + data.size());
  int cv_read_flag = (is_color ? CV_LOAD_IMAGE_COLOR :
    CV_LOAD_IMAGE_GRAYSCALE);
  cv_img = cv::imdecode(vec_data, cv_read_flag);
  if (!cv_img.data) {
    LOG(ERROR) << "Could not decode datum ";
  }
}

// If Datum is encoded will decoded using DecodeDatumToCVMat and CVMatToDatum
// If Datum is not encoded will do nothing
bool DecodeDatumNative(Datum* datum) {
  if (datum->encoded()) {
    cv::Mat cv_img = DecodeDatumToCVMatNative((*datum));
    CVMatToDatum(cv_img, *datum);
    return true;
  } else {
    return false;
  }
}
bool DecodeDatum(Datum* datum, bool is_color) {
  if (datum->encoded()) {
    cv::Mat cv_img = DecodeDatumToCVMat((*datum), is_color);
    CVMatToDatum(cv_img, *datum);
    return true;
  } else {
    return false;
  }
}

void DatumToCVMat(const Datum& datum, cv::Mat& img) {
  if (datum.encoded()) {
    LOG(FATAL) << "Datum encoded";
  }
  const int datum_channels = datum.channels();
  const int datum_height = datum.height();
  const int datum_width = datum.width();
  const int datum_size = datum_channels * datum_height * datum_width;
  CHECK_GT(datum_channels, 0);
  CHECK_GT(datum_height, 0);
  CHECK_GT(datum_width, 0);
  img = cv::Mat::zeros(cv::Size(datum_width, datum_height), CV_8UC(datum_channels));
  CHECK_EQ(img.channels(), datum_channels);
  CHECK_EQ(img.rows, datum_height);
  CHECK_EQ(img.cols, datum_width);
  const std::string& datum_buf = datum.data();
  CHECK_EQ(datum_buf.size(), datum_size);
  const int datum_hw_stride = datum_height * datum_width;
  for (int h = 0; h < datum_height; ++h) {
    const int datum_h_offset = h * datum_width;
    uchar* img_row_ptr = img.ptr<uchar>(h);
    int img_row_index = 0;
    for (int w = 0; w < datum_width; ++w) {
      int datum_index = datum_h_offset + w;
      for (int c = 0; c < datum_channels; ++c, datum_index += datum_hw_stride) {
        img_row_ptr[img_row_index++] = datum_buf[datum_index];
      }
    }
  }
}

void CVMatToDatum(const cv::Mat& cv_img, Datum& datum) {
  CHECK(cv_img.depth() == CV_8U) << "Image data type must be unsigned byte";
  const unsigned int img_channels = cv_img.channels();
  const unsigned int img_height = cv_img.rows;
  const unsigned int img_width = cv_img.cols;
  const unsigned int img_size = img_channels * img_height * img_width;
  CHECK_GT(img_channels, 0);
  CHECK_GT(img_height, 0);
  CHECK_GT(img_width, 0);
  string* img_buf = datum.release_data();
  delete img_buf;
  img_buf = new string(img_size, 0);
  const unsigned int datum_hw_stride = img_height * img_width;
  for (unsigned int h = 0; h < img_height; ++h) {
    const unsigned int datum_h_offset = h * img_width;
    const uchar* row_ptr = cv_img.ptr<uchar>(h);
    unsigned int row_index = 0;
    for (unsigned int w = 0; w < img_width; ++w) {
      unsigned int datum_index = datum_h_offset + w;
      for (unsigned int c = 0; c < img_channels; ++c, datum_index += datum_hw_stride) {
        img_buf->at(datum_index) = static_cast<char>(row_ptr[row_index++]);
      }
    }
  }
  datum.set_allocated_data(img_buf);
  datum.set_channels(img_channels);
  datum.set_height(img_height);
  datum.set_width(img_width);
  datum.set_encoded(false);
}

#endif  // USE_OPENCV
}  // namespace caffe
