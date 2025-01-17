#include "flag.h"
#include "nndeploy/base/glic_stl_include.h"
#include "nndeploy/base/shape.h"
#include "nndeploy/base/time_profiler.h"
#include "nndeploy/dag/node.h"
#include "nndeploy/device/device.h"
#include "nndeploy/model/detect/yolo/yolo.h"
#include "nndeploy/thread_pool/thread_pool.h"

using namespace nndeploy;

class OpParam : public base::Param {
 public:
  base::DataType data_type_ = base::dataTypeOf<float>();
  base::DataFormat data_format_ = base::DataFormat::kDataFormatNCHW;
  base::IntVector shape_ = {1, 3, 512, 512};

  size_t execute_time_ = 10;
};

class NNDEPLOY_CC_API OpNode : public dag::Node {
 public:
  OpNode(const std::string &name, dag::Edge *input, dag::Edge *output)
      : Node(name, input, output) {
    param_ = std::make_shared<OpParam>();
    OpParam *op_param = dynamic_cast<OpParam *>(param_.get());
  }
  virtual ~OpNode() {}

  virtual base::Status run() {
    // NNDEPLOY_LOGE("Node name[%s], Thread ID: %d.\n", name_.c_str(),
    //               std::this_thread::get_id());
    OpParam *tmp_param = dynamic_cast<OpParam *>(param_.get());
    device::Tensor *src = inputs_[0]->getTensor(this);
    device::Device *device = device::getDefaultHostDevice();
    device::TensorDesc desc;
    desc.data_type_ = tmp_param->data_type_;
    desc.data_format_ = tmp_param->data_format_;
    desc.shape_ = tmp_param->shape_;
    device::Tensor *dst =
        outputs_[0]->create(device, desc, inputs_[0]->getIndex(this));

    // execute time
    std::this_thread::sleep_for(
        std::chrono::milliseconds(tmp_param->execute_time_));

    outputs_[0]->notifyWritten(dst);
    return base::kStatusCodeOk;
  }
};

int serialGraph(dag::ParallelType pt_0, dag::ParallelType pt_1,
                dag::ParallelType pt, int count = 16) {
  // construct graph
  dag::Edge sub_in_0("sub_in_0");
  dag::Edge sub_out_0("sub_out_0");
  dag::Graph *sub_graph_0 =
      new dag::Graph("sub_graph_0", &sub_in_0, &sub_out_0);
  dag::Edge *op_0_1_out = sub_graph_0->createEdge("op_1_1_out");
  dag::Node *op_0_1 =
      sub_graph_0->createNode<OpNode>("op_0_1", &sub_in_0, op_0_1_out);
  dag::Node *op_0_2 =
      sub_graph_0->createNode<OpNode>("op_0_2", op_0_1_out, &sub_out_0);
  base::Status status = sub_graph_0->setParallelType(pt_0);

  dag::Edge sub_in_1("sub_in_1");
  dag::Edge sub_out_1("sub_out_1");
  dag::Graph *sub_graph_1 =
      new dag::Graph("sub_graph_1", &sub_in_1, &sub_out_1);
  dag::Edge *op_1_1_out = sub_graph_1->createEdge("op_1_1_out");
  dag::Node *op_1_1 =
      sub_graph_1->createNode<OpNode>("op_1_1", &sub_in_1, op_1_1_out);
  dag::Node *op_1_2 =
      sub_graph_1->createNode<OpNode>("op_1_2", op_1_1_out, &sub_out_1);
  status = sub_graph_1->setParallelType(pt_1);

  dag::Graph *graph = new dag::Graph("graph", &sub_in_0, &sub_out_1);
  graph->addNode(sub_graph_0);
  dag::Node *op_link =
      graph->createNode<OpNode>("op_link", &sub_out_0, &sub_in_1);
  graph->addNode(sub_graph_1);
  status = graph->setParallelType(pt);

  // init
  status = graph->init();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph init failed.\n");
    return -1;
  }

  // dump
  status = graph->dump();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph dump failed.\n");
    return -1;
  }

  // run
  for (int i = 0; i < count; ++i) {
    // set input
    device::Device *device = device::getDefaultHostDevice();
    device::TensorDesc desc;
    desc.data_type_ = base::dataTypeOf<float>();
    desc.data_format_ = base::DataFormat::kDataFormatNCHW;
    desc.shape_ = {1, 3, 512, 512};
    device::Tensor *input_tensor =
        new device::Tensor(device, desc, "sub_in_0 ");
    sub_in_0.set(input_tensor, i, false);

    // run
    status = graph->run();
    if (status != base::kStatusCodeOk) {
      NNDEPLOY_LOGE("graph dump failed.\n");
      return -1;
    }

    // get output (not kParallelTypePipeline)
    if (pt != dag::kParallelTypePipeline) {
      device::Tensor *result = sub_out_1.getGraphOutputTensor();
      if (result == nullptr) {
        NNDEPLOY_LOGE("result is nullptr");
        return -1;
      }
    }
  }
  // get output (kParallelTypePipeline)
  if (pt == dag::kParallelTypePipeline) {
    for (int i = 0; i < count; ++i) {
      device::Tensor *result = sub_out_1.getGraphOutputTensor();
      if (result == nullptr) {
        NNDEPLOY_LOGE("result is nullptr");
        return -1;
      }
    }
  }

  // 有向无环图graph反初始化
  status = graph->deinit();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph deinit failed");
    return -1;
  }

  // 有向无环图graph销毁
  delete sub_graph_0;
  delete sub_graph_1;
  delete graph;

  return 0;
}

int parallelGraph(dag::ParallelType pt_0, dag::ParallelType pt_1,
                  dag::ParallelType pt, int count = 16) {
  // construct graph
  dag::Edge sub_in_0("sub_in_0");
  dag::Edge sub_out_0("sub_out_0");
  dag::Graph *sub_graph_0 =
      new dag::Graph("sub_graph_0", &sub_in_0, &sub_out_0);
  dag::Edge *op_0_1_out = sub_graph_0->createEdge("op_1_1_out");
  dag::Node *op_0_1 =
      sub_graph_0->createNode<OpNode>("op_0_1", &sub_in_0, op_0_1_out);
  dag::Node *op_0_2 =
      sub_graph_0->createNode<OpNode>("op_0_2", op_0_1_out, &sub_out_0);
  base::Status status = sub_graph_0->setParallelType(pt_0);

  dag::Edge sub_out_1("sub_out_1");
  dag::Graph *sub_graph_1 =
      new dag::Graph("sub_graph_1", &sub_in_0, &sub_out_1);
  dag::Edge *op_1_1_out = sub_graph_1->createEdge("op_1_1_out");
  dag::Node *op_1_1 =
      sub_graph_1->createNode<OpNode>("op_1_1", &sub_in_0, op_1_1_out);
  dag::Node *op_1_2 =
      sub_graph_1->createNode<OpNode>("op_1_2", op_1_1_out, &sub_out_1);
  status = sub_graph_1->setParallelType(pt_1);

  dag::Edge sub_out_2("sub_out_2");
  dag::Graph *graph = new dag::Graph("graph", {&sub_in_0},
                                     {&sub_out_0, &sub_out_1, &sub_out_2});
  graph->addNode(sub_graph_0);
  graph->addNode(sub_graph_1);
  dag::Node *op_parallel_out =
      graph->createNode<OpNode>("op_parallel_out", &sub_out_1, &sub_out_2);
  status = graph->setParallelType(pt);

  // init
  status = graph->init();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph init failed.\n");
    return -1;
  }

  // dump
  status = graph->dump();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph dump failed.\n");
    return -1;
  }

  // run
  for (int i = 0; i < count; ++i) {
    device::Device *device = device::getDefaultHostDevice();
    device::TensorDesc desc;
    desc.data_type_ = base::dataTypeOf<float>();
    desc.data_format_ = base::DataFormat::kDataFormatNCHW;
    desc.shape_ = {1, 3, 512, 512};
    device::Tensor *input_tensor =
        new device::Tensor(device, desc, "sub_in_0 ");
    sub_in_0.set(input_tensor, i, false);

    graph->run();
    if (status != base::kStatusCodeOk) {
      NNDEPLOY_LOGE("graph dump failed.\n");
      return -1;
    }

    // get output (not kParallelTypePipeline)
    if (pt != dag::kParallelTypePipeline) {
      device::Tensor *result_1 = sub_out_1.getGraphOutputTensor();
      if (result_1 == nullptr) {
        NNDEPLOY_LOGE("result_1 is nullptr");
        return -1;
      }
      device::Tensor *result_2 = sub_out_2.getGraphOutputTensor();
      if (result_2 == nullptr) {
        NNDEPLOY_LOGE("result_1 is nullptr");
        return -1;
      }
    }
  }
  if (pt == dag::kParallelTypePipeline) {
    for (int i = 0; i < count; ++i) {
      device::Tensor *result_1 = sub_out_1.getGraphOutputTensor();
      if (result_1 == nullptr) {
        NNDEPLOY_LOGE("result_1 is nullptr");
        return -1;
      }
      device::Tensor *result_2 = sub_out_2.getGraphOutputTensor();
      if (result_2 == nullptr) {
        NNDEPLOY_LOGE("result is nullptr");
        return -1;
      }
    }
  }

  // 有向无环图graph反初始化
  status = graph->deinit();
  if (status != base::kStatusCodeOk) {
    NNDEPLOY_LOGE("graph deinit failed");
    return -1;
  }

  // 有向无环图graph销毁
  delete sub_graph_0;
  delete sub_graph_1;
  delete graph;

  return 0;
}

int main(int argc, char *argv[]) {
  NNDEPLOY_LOGE("start!\n");
  int ret = 0;

  int count = 8;
  for (int i = 0; i < count; i++) {
    // sequential graph
    ret =
        serialGraph(dag::kParallelTypeSequential, dag::kParallelTypeSequential,
                    dag::kParallelTypeSequential);
    if (ret != 0) {
      return ret;
    }
    ret = parallelGraph(dag::kParallelTypeSequential,
                        dag::kParallelTypeSequential,
                        dag::kParallelTypeSequential);
    if (ret != 0) {
      return ret;
    }
    // parallel task grah
    ret = serialGraph(dag::kParallelTypeTask, dag::kParallelTypeTask,
                      dag::kParallelTypeTask);
    if (ret != 0) {
      return ret;
    }
    ret = parallelGraph(dag::kParallelTypeTask, dag::kParallelTypeTask,
                        dag::kParallelTypeTask);
    if (ret != 0) {
      return ret;
    }
    // parallel pipepline graph
    ret = serialGraph(dag::kParallelTypeNone, dag::kParallelTypeNone,
                      dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }
    ret = parallelGraph(dag::kParallelTypeNone, dag::kParallelTypeNone,
                        dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }
    // parallel pipepline graph / sugraph sequential
    ret = serialGraph(dag::kParallelTypeSequential,
                      dag::kParallelTypeSequential, dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }
    ret =
        parallelGraph(dag::kParallelTypeSequential,
                      dag::kParallelTypeSequential, dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }
    // parallel pipepline graph / sugraph task
    ret = serialGraph(dag::kParallelTypeTask, dag::kParallelTypeTask,
                      dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }
    ret = parallelGraph(dag::kParallelTypeTask, dag::kParallelTypeTask,
                        dag::kParallelTypePipeline);
    if (ret != 0) {
      return ret;
    }

    // TODO
    // loop graph - 暂不支持流水线并行模式
    // condition graph
    // condition running graph
  }

  NNDEPLOY_LOGE("end!\n");

  return ret;
}