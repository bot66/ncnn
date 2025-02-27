// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "onnx.pb.h"

#include <algorithm>
#include <float.h>
#include <fstream>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <iostream>
#include <limits.h>
#include <limits>
#include <set>
#include <stdio.h>

static bool read_proto_from_binary(const char* filepath, onnx::ModelProto* message)
{
    std::ifstream fs(filepath, std::ifstream::in | std::ifstream::binary);
    if (!fs.is_open())
    {
        fprintf(stderr, "open failed %s\n", filepath);
        return false;
    }

    google::protobuf::io::IstreamInputStream input(&fs);
    google::protobuf::io::CodedInputStream codedstr(&input);

#if GOOGLE_PROTOBUF_VERSION >= 3011000
    codedstr.SetTotalBytesLimit(INT_MAX);
#else
    codedstr.SetTotalBytesLimit(INT_MAX, INT_MAX / 2);
#endif

    bool success = message->ParseFromCodedStream(&codedstr);

    fs.close();

    return success;
}

static std::vector<int> get_node_attr_ai(const onnx::NodeProto& node, const char* key)
{
    std::vector<int> v;

    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            v.resize(attr.ints_size());
            for (int j = 0; j < attr.ints_size(); j++)
            {
                v[j] = std::max(std::min(attr.ints(j), (::google::protobuf::int64)INT_MAX), (::google::protobuf::int64)INT_MIN);
            }

            break;
        }
    }

    return v;
}

static std::vector<float> get_node_attr_af(const onnx::NodeProto& node, const char* key)
{
    std::vector<float> v;

    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            v.resize(attr.floats_size());
            for (int j = 0; j < attr.floats_size(); j++)
            {
                v[j] = attr.floats(j);
            }

            break;
        }
    }

    return v;
}

static int get_node_attr_i(const onnx::NodeProto& node, const char* key, int def = 0)
{
    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            return std::max(std::min(attr.i(), (::google::protobuf::int64)INT_MAX), (::google::protobuf::int64)INT_MIN);
        }
    }

    return def;
}

static float get_node_attr_f(const onnx::NodeProto& node, const char* key, float def = 0.f)
{
    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            return attr.f();
        }
    }

    return def;
}

static std::string get_node_attr_s(const onnx::NodeProto& node, const char* key, const std::string& def = std::string())
{
    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            return attr.s();
        }
    }

    return def;
}

static onnx::TensorProto get_node_attr_tensor(const onnx::NodeProto& node, const char* key)
{
    for (int i = 0; i < node.attribute_size(); i++)
    {
        const onnx::AttributeProto& attr = node.attribute(i);
        if (attr.name() == key)
        {
            return attr.t();
        }
    }

    return onnx::TensorProto();
}

static float get_node_attr_from_input_f(const onnx::TensorProto& tp)
{
    float v = 0.f;

    // float
    if (tp.data_type() == 1)
    {
        const float* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const float*)tp.raw_data().data();
        }
        else
        {
            shape_data = tp.float_data().data();
        }
        v = shape_data[0];
    }
    // double
    else if (tp.data_type() == 11)
    {
        const double* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const double*)tp.raw_data().data();
        }
        else
        {
            shape_data = tp.double_data().data();
        }
        v = shape_data[0];
    }
    // int64
    else if (tp.data_type() == 7)
    {
        const int64_t* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const int64_t*)tp.raw_data().data();
        }
        else
        {
            shape_data = tp.int64_data().data();
        }
        v = std::max(std::min(shape_data[0], (::google::protobuf::int64)INT_MAX), (::google::protobuf::int64)INT_MIN);
    }
    // int32
    else if (tp.data_type() == 6)
    {
        const int32_t* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const int32_t*)tp.raw_data().data();
        }
        else
        {
            shape_data = tp.int32_data().data();
        }
        v = shape_data[0];
    }
    else
    {
        fprintf(stderr, "Unknown data type %d\n", tp.data_type());
        abort();
    }

    return v;
}

static std::vector<int> get_node_attr_from_input_ai(const onnx::TensorProto& tp)
{
    int size = 0;

    std::vector<int> v;

    // int64
    if (tp.data_type() == 7)
    {
        const int64_t* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const int64_t*)tp.raw_data().data();
            size = (int)(tp.raw_data().size() / 8);
        }
        else
        {
            shape_data = tp.int64_data().data();
            size = tp.int64_data_size();
        }
        for (int j = 0; j < size; j++)
        {
            int vi = std::max(std::min(shape_data[j], (::google::protobuf::int64)INT_MAX), (::google::protobuf::int64)INT_MIN);
            v.push_back(vi);
        }
    }
    // int32
    else if (tp.data_type() == 6)
    {
        const int32_t* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const int32_t*)tp.raw_data().data();
            size = (int)(tp.raw_data().size() / 4);
        }
        else
        {
            shape_data = tp.int32_data().data();
            size = tp.int32_data_size();
        }
        for (int j = 0; j < size; j++)
        {
            v.push_back(shape_data[j]);
        }
    }
    else
    {
        fprintf(stderr, "Unknown data type %d\n", tp.data_type());
    }

    return v;
}

static std::vector<float> get_node_attr_from_input_af(const onnx::TensorProto& tp)
{
    int size = 0;

    std::vector<float> v;

    // float
    if (tp.data_type() == 1)
    {
        const float* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const float*)tp.raw_data().data();
            size = (int)(tp.raw_data().size() / 4);
        }
        else
        {
            shape_data = tp.float_data().data();
            size = tp.float_data_size();
        }
        for (int j = 0; j < size; j++)
        {
            v.push_back(shape_data[j]);
        }
    }
    // double
    else if (tp.data_type() == 11)
    {
        const double* shape_data = 0;
        if (tp.has_raw_data())
        {
            shape_data = (const double*)tp.raw_data().data();
            size = (int)(tp.raw_data().size() / 8);
        }
        else
        {
            shape_data = tp.double_data().data();
            size = tp.double_data_size();
        }
        for (int j = 0; j < size; j++)
        {
            v.push_back((float)shape_data[j]);
        }
    }
    else
    {
        fprintf(stderr, "Unknown data type %d\n", tp.data_type());
    }

    return v;
}

static int get_tensor_proto_data_size(const onnx::TensorProto& tp)
{
    if (tp.has_raw_data())
    {
        const std::string& raw_data = tp.raw_data();
        int size = (int)raw_data.size() / 4;
        return size;
    }
    else if (tp.data_type() == 1)
    {
        return tp.float_data_size();
    }

    return 0;
}

static void fwrite_tensor_proto_data(const onnx::TensorProto& tp, FILE* bp)
{
    int size = get_tensor_proto_data_size(tp);

    if (tp.has_raw_data())
    {
        const std::string& raw_data = tp.raw_data();
        fwrite(raw_data.data(), sizeof(float), size, bp);
    }
    else if (tp.data_type() == 1)
    {
        fwrite(tp.float_data().data(), sizeof(float), size, bp);
    }
}

static void fuse_weight_reshape(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // weight <= Reshape(weight)
        if (node->op_type() == "Reshape")
        {
            // check weight
            if (weights.find(node->input(0)) == weights.end())
                continue;

            weights[node->output(0)] = weights[node->input(0)];

            // set weight shape directly
            std::vector<int> shape;
            if (node->input_size() == 1)
            {
                shape = get_node_attr_ai(*node, "shape");
            }
            else if (node->input_size() == 2)
            {
                // opset 5
                shape = get_node_attr_from_input_ai(weights[node->input(1)]);
            }

            weights[node->output(0)].clear_dims();
            for (int j = 0; j < shape.size(); j++)
            {
                weights[node->output(0)].add_dims(shape[j]);
            }

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;
            if (node->input_size() == 2)
            {
                node_reference[node->input(1)] -= 1;
            }

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_weight_transpose(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // weight <= Transpose(weight)
        if (node->op_type() == "Transpose")
        {
            // check weight
            if (weights.find(node->input(0)) == weights.end())
                continue;

            if (weights[node->input(0)].dims_size() != 2)
                continue;

            // perm = (1, 0)
            std::vector<int> perm = get_node_attr_ai(*node, "perm");
            if (perm.size() != 2)
                continue;
            if (perm[0] != 1 || perm[1] != 0)
                continue;

            weights[node->output(0)] = weights[node->input(0)];

            // permute weight
            {
                onnx::TensorProto& B = weights[node->output(0)];

                const int h = B.dims(0);
                const int w = B.dims(1);

                std::vector<float> permuted_data;
                permuted_data.reserve((size_t)h * w);
                const float* bptr = B.has_raw_data() ? (const float*)B.raw_data().data() : B.float_data().data();

                for (int j = 0; j < w; j++)
                {
                    for (int k = 0; k < h; k++)
                    {
                        float vb = bptr[k * w + j];
                        permuted_data.push_back(vb);
                    }
                }

                B.set_dims(0, w);
                B.set_dims(1, h);

                if (B.has_raw_data())
                {
                    B.set_raw_data(permuted_data.data(), permuted_data.size() * sizeof(float));
                }
                else
                {
                    for (int j = 0; j < (int)permuted_data.size(); j++)
                        B.set_float_data(j, permuted_data[j]);
                }
            }

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_shufflechannel(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // ShuffleChannel <= Reshape - Transpose - Reshape
        // ShuffleChannel <= Reshape - Transpose - Constant - Reshape
        if (node->op_type() == "Reshape")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            std::vector<int> shape;
            if (node->input_size() == 1)
            {
                shape = get_node_attr_ai(*node, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node->input(1)) == weights.end())
                    continue;

                shape = get_node_attr_from_input_ai(weights[node->input(1)]);
            }

            // 1 groups channels_per_group, height, width
            // reverse style = channels_per_group, groups, height * width
            if (shape.size() != 5 && shape.size() != 3)
                continue;

            if (shape.size() == 5 && shape[0] != 1)
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node3->op_type() == "Constant")
            {
                if (i + 3 >= node_count)
                    continue;

                node3 = mutable_graph->mutable_node(i + 3);
            }

            if (node2->op_type() != "Transpose" || node3->op_type() != "Reshape")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            // 0 2 1 3 4
            // reverse style = 1 0 2
            std::vector<int> perm = get_node_attr_ai(*node2, "perm");
            if (perm.size() != 5 && perm.size() != 3)
                continue;

            if (perm.size() == 5 && (perm[0] != 0 || perm[1] != 2 || perm[2] != 1 || perm[3] != 3 || perm[4] != 4))
                continue;

            if (perm.size() == 3 && (perm[0] != 1 || perm[1] != 0 || perm[2] != 2))
                continue;

            std::vector<int> shape3;
            if (node3->input_size() == 1)
            {
                shape3 = get_node_attr_ai(*node3, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node3->input(1)) == weights.end())
                    continue;

                shape3 = get_node_attr_from_input_ai(weights[node3->input(1)]);
            }

            // 1, -1, height, width
            // reverse style = group, -1, channels_per_group, height, width
            if (shape3.size() != 4 && shape3.size() != 5)
                continue;

            if (shape3.size() == 4 && (shape3[0] != 1 || (shape3[1] != -1 && shape3[1] != shape[1] * shape[2])))
                continue;

            if (shape3.size() == 5 && (shape3[0] != shape[1] || shape3[2] != shape[0] || shape3[3] * shape3[4] != shape[2]))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");

            if (node->input_size() == 2)
            {
                node_reference[node->input(1)] -= 1;
            }
            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (node3->input_size() == 2)
            {
                node_reference[node3->input(1)] -= 1;
            }

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));

            node3->set_op_type("ShuffleChannel");
            node3->set_input(0, node->input(0));

            onnx::AttributeProto* attr_group = node3->add_attribute();
            attr_group->set_name("group");
            attr_group->set_i(shape[1]);

            onnx::AttributeProto* attr_reverse = node3->add_attribute();
            attr_reverse->set_name("reverse");
            attr_reverse->set_i(shape.size() == 3);

            reduced_node_count += 2;
            i += 2;
        }
    }
}

static void fuse_shufflechannel_split(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // Split <= ShuffleChannel(reverse type) - Gather(0) - Gather(1)
        if (node->op_type() == "ShuffleChannel")
        {
            // reverse = 1
            int reverse = get_node_attr_i(*node, "reverse");
            if (reverse != 1)
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node2->op_type() != "Gather" || node3->op_type() != "Gather")
                continue;

            if (node2->input(0) != node->output(0) || node3->input(0) != node->output(0))
                continue;

            // axis = 0
            int gather2_axis = get_node_attr_i(*node2, "axis");
            if (gather2_axis != 0)
                continue;

            // indices = 0
            if (weights.find(node2->input(1)) == weights.end())
                continue;

            std::vector<int> gather2_indices = get_node_attr_from_input_ai(weights[node2->input(1)]);
            if (gather2_indices.size() != 1 || gather2_indices[0] != 0)
                continue;

            // axis = 0
            int gather3_axis = get_node_attr_i(*node3, "axis");
            if (gather3_axis != 0)
                continue;

            // indices = 1
            if (weights.find(node3->input(1)) == weights.end())
                continue;

            std::vector<int> gather3_indices = get_node_attr_from_input_ai(weights[node3->input(1)]);
            if (gather3_indices.size() != 1 || gather3_indices[0] != 1)
                continue;

            // reduce
            node2->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;
            node_reference[node2->input(1)] -= 1;
            node_reference[node2->output(0)] -= 1;
            node_reference[node3->input(1)] -= 1;

            blob_names.erase(node2->output(0));

            node3->set_op_type("Split");
            node3->clear_input();
            node3->add_input(node->output(0));
            node3->add_output(node3->output(0));
            node3->set_output(0, node2->output(0));

            node3->clear_attribute();
            onnx::AttributeProto* attr_axis = node3->add_attribute();
            attr_axis->set_name("axis");
            attr_axis->set_i(1);

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_hardswish(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // HardSwish <= Add(+3) - Clip(0,6) - Mul(X,) - Div(/6)
        // HardSwish <= Add(+3) - Clip(0,6) - Mul(X,) - Mul(*(1/6))
        // HardSwish <= Add(+3) - Clip(0,6) - Mul(X,) - Constant - Div(/6)
        // HardSwish <= Add(+3) - Clip(0,6) - Mul(X,) - Constant - Mul(*(1/6))
        //     out = x * F.relu6(x + 3, inplace=True) / 6
        if (node->op_type() == "Add")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 3 >= node_count)
                continue;

            if (weights.find(node->input(1)) == weights.end())
                continue;

            const onnx::TensorProto& add_three = weights[node->input(1)];
            if (add_three.dims_size() != 0 || get_tensor_proto_data_size(add_three) != 1)
                continue;

            float constant_add_three = get_node_attr_from_input_f(add_three);
            if (constant_add_three != 3.f)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);
            onnx::NodeProto* node4 = mutable_graph->mutable_node(i + 3);

            if (node4->op_type() == "Constant")
            {
                if (i + 4 >= node_count)
                    continue;

                node4 = mutable_graph->mutable_node(i + 4);
            }

            if (node2->op_type() != "Clip" || node3->op_type() != "Mul" || (node4->op_type() != "Div" && node4->op_type() != "Mul"))
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            float relu6_min;
            float relu6_max;
            if (node2->input_size() == 1)
            {
                relu6_min = get_node_attr_f(*node2, "min", -FLT_MAX);
                relu6_max = get_node_attr_f(*node2, "max", FLT_MAX);
            }
            else
            {
                const onnx::TensorProto& min_tp = weights[node2->input(1)];
                const onnx::TensorProto& max_tp = weights[node2->input(2)];

                relu6_min = get_node_attr_from_input_f(min_tp);
                relu6_max = get_node_attr_from_input_f(max_tp);
            }
            if (relu6_min != 0.f || relu6_max != 6.f)
                continue;

            if (node_reference[node3->output(0)] != 1)
                continue;

            if (node3->input(0) != node->input(0) || node3->input(1) != node2->output(0))
                continue;

            if (weights.find(node4->input(1)) == weights.end())
                continue;

            const onnx::TensorProto& div_six = weights[node4->input(1)];
            if (div_six.dims_size() != 0 || get_tensor_proto_data_size(div_six) != 1)
                continue;

            float constant_div_six = get_node_attr_from_input_f(div_six);
            if (node4->op_type() == "Div" && constant_div_six != 6.f)
                continue;
            if (node4->op_type() == "Mul" && constant_div_six != 1 / 6.f)
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");
            node3->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;
            node_reference[node->input(1)] -= 1;
            node_reference[node->output(0)] -= 1;
            if (node2->input_size() == 3)
            {
                node_reference[node2->input(1)] -= 1;
                node_reference[node2->input(2)] -= 1;
            }
            node_reference[node2->output(0)] -= 1;
            node_reference[node3->output(0)] -= 1;
            node_reference[node4->input(1)] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));
            blob_names.erase(node3->output(0));

            node4->set_op_type("HardSwish");
            node4->clear_input();
            node4->add_input(node->input(0));

            onnx::AttributeProto* attr_alpha = node4->add_attribute();
            attr_alpha->set_name("alpha");
            attr_alpha->set_f(1.f / 6.f);

            onnx::AttributeProto* attr_beta = node4->add_attribute();
            attr_beta->set_name("beta");
            attr_beta->set_f(3.f / 6.f);

            reduced_node_count += 3;
            i += 3;
        }
    }

    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // HardSwish <= HardSigmoid - Mul
        //     out = x * hsigmoid(x)
        if (node->op_type() == "HardSigmoid")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            float alpha = get_node_attr_f(*node, "alpha", 0.2f);
            float beta = get_node_attr_f(*node, "beta", 0.5f);

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "Mul")
                continue;

            if (node2->input(0) != node->input(0) || node2->input(1) != node->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;
            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));

            node2->set_op_type("HardSwish");
            node2->clear_input();
            node2->add_input(node->input(0));

            onnx::AttributeProto* attr_alpha = node2->add_attribute();
            attr_alpha->set_name("alpha");
            attr_alpha->set_f(alpha);

            onnx::AttributeProto* attr_beta = node2->add_attribute();
            attr_beta->set_name("beta");
            attr_beta->set_f(beta);

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_hardsigmoid(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // HardSigmoid <= Add(+3) - Clip(0,6) - Div(/6)
        // HardSigmoid <= Add(+3) - Clip(0,6) - Mul(*(1/6))
        // HardSigmoid <= Add(+3) - Clip(0,6) - Constant - Div(/6)
        // HardSigmoid <= Add(+3) - Clip(0,6) - Constant - Mul(*(1/6))
        //     out = F.relu6(x + 3, inplace=True) / 6
        if (node->op_type() == "Add")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 2 >= node_count)
                continue;

            if (weights.find(node->input(1)) == weights.end())
                continue;

            const onnx::TensorProto& add_three = weights[node->input(1)];
            if (add_three.dims_size() != 0 || get_tensor_proto_data_size(add_three) != 1)
                continue;

            float constant_add_three = get_node_attr_from_input_f(add_three);
            if (constant_add_three != 3.f)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node3->op_type() == "Constant")
            {
                if (i + 3 >= node_count)
                    continue;

                node3 = mutable_graph->mutable_node(i + 3);
            }

            if (node2->op_type() != "Clip" || (node3->op_type() != "Div" && node3->op_type() != "Mul"))
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            float relu6_min;
            float relu6_max;
            if (node2->input_size() == 1)
            {
                relu6_min = get_node_attr_f(*node2, "min", -FLT_MAX);
                relu6_max = get_node_attr_f(*node2, "max", FLT_MAX);
            }
            else
            {
                const onnx::TensorProto& min_tp = weights[node2->input(1)];
                const onnx::TensorProto& max_tp = weights[node2->input(2)];

                relu6_min = get_node_attr_from_input_f(min_tp);
                relu6_max = get_node_attr_from_input_f(max_tp);
            }
            if (relu6_min != 0.f || relu6_max != 6.f)
                continue;

            if (weights.find(node3->input(1)) == weights.end())
                continue;

            const onnx::TensorProto& div_six = weights[node3->input(1)];
            if (div_six.dims_size() != 0 || get_tensor_proto_data_size(div_six) != 1)
                continue;

            float constant_div_six = get_node_attr_from_input_f(div_six);
            if (node3->op_type() == "Div" && constant_div_six != 6.f)
                continue;
            if (node3->op_type() == "Mul" && constant_div_six != 1 / 6.f)
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");

            node_reference[node->input(1)] -= 1;
            node_reference[node->output(0)] -= 1;
            if (node2->input_size() == 3)
            {
                node_reference[node2->input(1)] -= 1;
                node_reference[node2->input(2)] -= 1;
            }
            node_reference[node2->output(0)] -= 1;
            node_reference[node3->input(1)] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));

            node3->set_op_type("HardSigmoid");
            node3->clear_input();
            node3->add_input(node->input(0));

            onnx::AttributeProto* attr_alpha = node3->add_attribute();
            attr_alpha->set_name("alpha");
            attr_alpha->set_f(1.f / 6.f);

            onnx::AttributeProto* attr_beta = node3->add_attribute();
            attr_beta->set_name("beta");
            attr_beta->set_f(3.f / 6.f);

            reduced_node_count += 2;
            i += 2;
        }
    }
}

static void fuse_swish(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // Swish <= Sigmoid - Mul
        //     x * torch.sigmoid(x)
        if (node->op_type() == "Sigmoid")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "Mul")
                continue;

            if (node2->input(0) != node->input(0) || node2->input(1) != node->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;
            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));

            node2->set_op_type("Swish");
            node2->clear_input();
            node2->add_input(node->input(0));

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_batchnorm1d_squeeze_unsqueeze(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // BatchNormalization <= Unsqueeze - BatchNormalization - Squeeze
        if (node->op_type() == "Unsqueeze")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node2->op_type() != "BatchNormalization" || node3->op_type() != "Squeeze")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            if (node2->input(0) != node->output(0) || node3->input(0) != node2->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node3->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));

            node2->set_input(0, node->input(0));
            node2->set_output(0, node3->output(0));

            reduced_node_count += 2;
            i += 2;
        }
    }
}

static void fuse_unsqueeze_prelu(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // PReLU <= Unsqueeze - PReLU
        if (node->op_type() == "Unsqueeze")
        {
            // check weight
            if (weights.find(node->input(0)) == weights.end())
                continue;

            onnx::TensorProto& B = weights[node->input(0)];
            if (B.dims_size() != 1)
                continue;

            if (node_reference[node->output(0)] != 1)
                continue;

            // axes = (1, 2)
            std::vector<int> axes = get_node_attr_ai(*node, "axes");
            if (axes.size() != 2)
                continue;
            if (axes[0] != 1 || axes[1] != 2)
                continue;

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "PRelu")
                continue;

            if (node2->input(1) != node->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));

            node2->set_input(1, node->input(0));

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_normalize(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // Normalize <= X - ReduceL2 - Clip - Expand - Div
        // Normalize <= X - ReduceL2 - Clip - Shape - Expand - Div
        if (node->op_type() == "ReduceL2")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            // axes = (1)
            std::vector<int> axes = get_node_attr_ai(*node, "axes");
            if (axes.size() != 1)
                continue;
            if (axes[0] != 1)
                continue;

            if (i + 3 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);
            onnx::NodeProto* node4 = mutable_graph->mutable_node(i + 3);

            bool has_shape_node = node3->op_type() == "Shape";
            onnx::NodeProto* node_shape = 0;
            if (has_shape_node)
            {
                if (i + 4 >= node_count)
                    continue;

                node_shape = node3;
                node3 = mutable_graph->mutable_node(i + 3);
                node4 = mutable_graph->mutable_node(i + 4);
            }

            if (node2->op_type() != "Clip" || node3->op_type() != "Expand" || node4->op_type() != "Div")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            if (node_reference[node3->output(0)] != 1)
                continue;

            if (node2->input(0) != node->output(0) || node3->input(0) != node2->output(0)
                    || node4->input(0) != node->input(0) || node4->input(1) != node3->output(0))
                continue;

            if (has_shape_node)
            {
                if (node_shape->input(0) != node->input(0) || node3->input(1) != node_shape->output(0))
                    continue;
            }

            // +eps
            float clip_min;
            if (node2->input_size() == 1)
            {
                clip_min = get_node_attr_f(*node2, "min", -FLT_MAX);
            }
            else
            {
                const onnx::TensorProto& min_tp = weights[node2->input(1)];

                clip_min = get_node_attr_from_input_f(min_tp);
            }

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");
            if (has_shape_node)
            {
                node_shape->set_op_type("noop_reducedncnn");
            }
            node3->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= has_shape_node ? 2 : 1;
            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (has_shape_node)
            {
                node_reference[node_shape->output(0)] -= 1;
            }
            node_reference[node3->output(0)] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));
            if (has_shape_node)
            {
                blob_names.erase(node_shape->output(0));
            }
            blob_names.erase(node3->output(0));

            node4->set_op_type("Normalize");
            node4->clear_input();
            node4->add_input(node->input(0));

            onnx::AttributeProto* attr_alpha = node4->add_attribute();
            attr_alpha->set_name("eps");
            attr_alpha->set_f(clip_min);

            reduced_node_count += has_shape_node ? 4 : 3;
            i += has_shape_node ? 4 : 3;
        }
    }
}

static void fuse_groupnorm(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // GroupNorm <= X - Reshape - InstanceNormalization - Reshape - Mul - Add
        if (node->op_type() == "Reshape")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            std::vector<int> shape;
            if (node->input_size() == 1)
            {
                shape = get_node_attr_ai(*node, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node->input(1)) == weights.end())
                    continue;

                shape = get_node_attr_from_input_ai(weights[node->input(1)]);
            }

            // 0, group, -1
            if (shape.size() != 3)
                continue;

            if (shape[0] != 0 || shape[2] != -1)
                continue;

            int groups = shape[1];

            if (i + 4 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);
            onnx::NodeProto* node4 = mutable_graph->mutable_node(i + 3);
            onnx::NodeProto* node5 = mutable_graph->mutable_node(i + 4);

            if (node2->op_type() != "InstanceNormalization" || node3->op_type() != "Reshape" || node4->op_type() != "Mul" || node5->op_type() != "Add")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            if (node_reference[node3->output(0)] != 1)
                continue;

            if (node_reference[node4->output(0)] != 1)
                continue;

            if (node2->input(0) != node->output(0) || node3->input(0) != node2->output(0)
                    || node4->input(0) != node3->output(0) || node5->input(0) != node4->output(0))
                continue;

            // +eps
            float eps = get_node_attr_f(*node2, "epsilon", 1e-05f);

            // InstanceNormalization S=1 B=0
            std::vector<float> S = get_node_attr_from_input_af(weights[node2->input(1)]);
            std::vector<float> B = get_node_attr_from_input_af(weights[node2->input(2)]);
            if ((int)S.size() != groups || (int)B.size() != groups)
                continue;

            bool instancenorm_affine = false;
            for (int j = 0; j < groups; j++)
            {
                if (S[j] != 1.f || B[j] != 0.f)
                {
                    instancenorm_affine = true;
                    break;
                }
            }

            if (instancenorm_affine)
                continue;

            std::vector<int> shape2;
            if (node3->input_size() == 1)
            {
                shape2 = get_node_attr_ai(*node3, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node3->input(1)) == weights.end())
                    continue;

                shape2 = get_node_attr_from_input_ai(weights[node3->input(1)]);
            }

            // 1, channels, w, h
            if (shape2.size() != 4)
                continue;

            if (shape2[0] != 1)
                continue;

            int channels = shape2[1];

            // affine
            int affine = 0;
            std::vector<float> affine_S = get_node_attr_from_input_af(weights[node4->input(1)]);
            std::vector<float> affine_B = get_node_attr_from_input_af(weights[node5->input(1)]);
            if (affine_S.size() == 1 && affine_S[0] == 1.f && affine_B.size() == 1 && affine_B[0] == 0.f)
            {
                affine = 0;
            }
            else if ((int)affine_S.size() != channels && (int)affine_B.size() != channels)
            {
                // we only allow per-channel affine
                continue;
            }

            for (int j = 0; j < channels; j++)
            {
                if (affine_S[j] != 1.f || affine_B[j] != 0.f)
                {
                    affine = 1;
                    break;
                }
            }

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");
            node3->set_op_type("noop_reducedncnn");
            node4->set_op_type("noop_reducedncnn");

            if (node->input_size() == 2)
            {
                node_reference[node->input(1)] -= 1;
            }
            node_reference[node->output(0)] -= 1;
            node_reference[node2->input(1)] -= 1;
            node_reference[node2->input(2)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (node3->input_size() == 2)
            {
                node_reference[node3->input(1)] -= 1;
            }
            node_reference[node3->output(0)] -= 1;
            node_reference[node4->output(0)] -= 1;

            std::string affine_scale = node4->input(1);
            std::string affine_bias = node5->input(1);

            node_reference[affine_scale] -= 1;
            node_reference[affine_bias] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));
            blob_names.erase(node3->output(0));
            blob_names.erase(node4->output(0));

            node5->set_op_type("GroupNorm");
            node5->clear_input();
            node5->add_input(node->input(0));
            if (affine)
            {
                node5->add_input(affine_scale);
                node5->add_input(affine_bias);
            }

            onnx::AttributeProto* attr_groups = node5->add_attribute();
            attr_groups->set_name("groups");
            attr_groups->set_i(groups);

            onnx::AttributeProto* attr_channels = node5->add_attribute();
            attr_channels->set_name("channels");
            attr_channels->set_i(channels);

            onnx::AttributeProto* attr_eps = node5->add_attribute();
            attr_eps->set_name("epsilon");
            attr_eps->set_f(eps);

            onnx::AttributeProto* attr_affine = node5->add_attribute();
            attr_affine->set_name("affine");
            attr_affine->set_i(affine);

            reduced_node_count += 4;
            i += 4;
        }
    }
}

static void fuse_flatten(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // Flatten <= X - Shape - Gather - Constant - Unsqueeze - Unsqueeze - Concat - Reshape
        if (node->op_type() == "Shape")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 6 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);
            onnx::NodeProto* node4 = mutable_graph->mutable_node(i + 3);
            onnx::NodeProto* node5 = mutable_graph->mutable_node(i + 4);
            onnx::NodeProto* node6 = mutable_graph->mutable_node(i + 5);
            onnx::NodeProto* node7 = mutable_graph->mutable_node(i + 6);

            if (node2->op_type() != "Gather" || node3->op_type() != "Constant" || node4->op_type() != "Unsqueeze" || node5->op_type() != "Unsqueeze"
                    || node6->op_type() != "Concat" || node7->op_type() != "Reshape")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            //             if (node_reference[node3->output(0)] != 1)
            //                 continue;

            if (node_reference[node4->output(0)] != 1)
                continue;

            if (node_reference[node5->output(0)] != 1)
                continue;

            if (node_reference[node6->output(0)] != 1)
                continue;

            if (node2->input(0) != node->output(0) || node4->input(0) != node2->output(0) || node5->input(0) != node3->output(0)
                    || node6->input(0) != node4->output(0) || node6->input(1) != node5->output(0)
                    || node7->input(0) != node->input(0) || node7->input(1) != node6->output(0))
                continue;

            // axis = 0
            int gather_axis = get_node_attr_i(*node2, "axis");
            if (gather_axis != 0)
                continue;

            // indices = 0
            if (weights.find(node2->input(1)) == weights.end())
                continue;

            std::vector<int> gather_indices = get_node_attr_from_input_ai(weights[node2->input(1)]);
            if (gather_indices.size() != 1 || gather_indices[0] != 0)
                continue;

            // axes = (0)
            std::vector<int> unsqueeze_axes = get_node_attr_ai(*node4, "axes");
            if (unsqueeze_axes.size() != 1)
                continue;
            if (unsqueeze_axes[0] != 0)
                continue;

            // axes = (0)
            std::vector<int> unsqueeze2_axes = get_node_attr_ai(*node5, "axes");
            if (unsqueeze2_axes.size() != 1)
                continue;
            if (unsqueeze2_axes[0] != 0)
                continue;

            // data = -1
            if (weights.find(node5->input(0)) == weights.end())
                continue;

            std::vector<int> unsqueeze2_data = get_node_attr_from_input_ai(weights[node5->input(0)]);
            if (unsqueeze2_data.size() != 1 || unsqueeze2_data[0] != -1)
                continue;

            // axis = 0
            int concat_axis = get_node_attr_i(*node6, "axis");
            if (concat_axis != 0)
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");
            //             node3->set_op_type("noop_reducedncnn");
            node4->set_op_type("noop_reducedncnn");
            node5->set_op_type("noop_reducedncnn");
            node6->set_op_type("noop_reducedncnn");

            node_reference[node->input(0)] -= 1;
            node_reference[node->output(0)] -= 1;
            node_reference[node2->input(1)] -= 1;
            node_reference[node2->output(0)] -= 1;
            //             node_reference[node3->output(0)] -= 1;
            node_reference[node4->output(0)] -= 1;
            node_reference[node5->input(0)] -= 1;
            node_reference[node5->output(0)] -= 1;
            node_reference[node6->output(0)] -= 1;

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));
            //             blob_names.erase(node3->output(0));
            blob_names.erase(node4->output(0));
            blob_names.erase(node5->output(0));
            blob_names.erase(node6->output(0));

            node7->set_op_type("Flatten");
            node7->clear_input();
            node7->add_input(node->input(0));

            reduced_node_count += 5;
            i += 5;
        }
    }
}

static void fuse_pixelshuffle(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // PixelShuffle <= Reshape - Transpose - Reshape
        // PixelShuffle <= Reshape - Transpose - Constant - Reshape
        if (node->op_type() == "Reshape")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            std::vector<int> shape;
            if (node->input_size() == 1)
            {
                shape = get_node_attr_ai(*node, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node->input(1)) == weights.end())
                    continue;

                shape = get_node_attr_from_input_ai(weights[node->input(1)]);
            }

            // -1, 3, upscale_factor, upscale_factor, height, width
            if (shape.size() != 6)
                continue;

            if (shape[0] != 1 && shape[0] != -1)
                continue;

            if (shape[2] != shape[3])
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node3->op_type() == "Constant")
            {
                if (i + 3 >= node_count)
                    continue;

                node3 = mutable_graph->mutable_node(i + 3);
            }

            if (node2->op_type() != "Transpose" || node3->op_type() != "Reshape")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            // 0 1 4 2 5 3
            std::vector<int> perm = get_node_attr_ai(*node2, "perm");
            if (perm.size() != 6)
                continue;

            if (perm[0] != 0 || perm[1] != 1 || perm[2] != 4 || perm[3] != 2 || perm[4] != 5 || perm[5] != 3)
                continue;

            std::vector<int> shape3;
            if (node3->input_size() == 1)
            {
                shape3 = get_node_attr_ai(*node3, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node3->input(1)) == weights.end())
                    continue;

                shape3 = get_node_attr_from_input_ai(weights[node3->input(1)]);
            }

            // -1, 3, height, width
            if (shape3.size() != 4)
                continue;

            if (shape3[0] != 1 && shape3[0] != -1)
                continue;

            if (shape3[1] != shape[1] || shape3[2] != shape[2] * shape[4] || shape3[3] != shape[3] * shape[5])
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");

            if (node->input_size() == 2)
            {
                node_reference[node->input(1)] -= 1;
            }
            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (node3->input_size() == 2)
            {
                node_reference[node3->input(1)] -= 1;
            }

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));

            node3->set_op_type("PixelShuffle");
            node3->set_input(0, node->input(0));

            onnx::AttributeProto* attr_group = node3->add_attribute();
            attr_group->set_name("scale_factor");
            attr_group->set_i(shape[2]);

            reduced_node_count += 2;
            i += 2;
        }
    }
}

static void fuse_reorg(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // PixelShuffle <= Reshape - Transpose - Reshape
        // PixelShuffle <= Reshape - Transpose - Constant - Reshape
        if (node->op_type() == "Reshape")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            std::vector<int> shape;
            if (node->input_size() == 1)
            {
                shape = get_node_attr_ai(*node, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node->input(1)) == weights.end())
                    continue;

                shape = get_node_attr_from_input_ai(weights[node->input(1)]);
            }

            // -1, 3, out_height, block_size, out_width, block_size
            if (shape.size() != 6)
                continue;

            if (shape[0] != 1 && shape[0] != -1)
                continue;

            if (shape[3] != shape[5])
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node3->op_type() == "Constant")
            {
                if (i + 3 >= node_count)
                    continue;

                node3 = mutable_graph->mutable_node(i + 3);
            }

            if (node2->op_type() != "Transpose" || node3->op_type() != "Reshape")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            // 0 1 3 5 2 4
            std::vector<int> perm = get_node_attr_ai(*node2, "perm");
            if (perm.size() != 6)
                continue;

            if (perm[0] != 0 || perm[1] != 1 || perm[2] != 3 || perm[3] != 5 || perm[4] != 2 || perm[5] != 4)
                continue;

            std::vector<int> shape3;
            if (node3->input_size() == 1)
            {
                shape3 = get_node_attr_ai(*node3, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node3->input(1)) == weights.end())
                    continue;

                shape3 = get_node_attr_from_input_ai(weights[node3->input(1)]);
            }

            // -1, out_channels, out_height, out_width
            if (shape3.size() != 4)
                continue;

            if (shape3[0] != 1 && shape3[0] != -1)
                continue;

            if (shape3[1] != shape[1] * shape[3] * shape[5] || shape3[2] != shape[2] || shape3[3] != shape[4])
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");
            node2->set_op_type("noop_reducedncnn");

            if (node->input_size() == 2)
            {
                node_reference[node->input(1)] -= 1;
            }
            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (node3->input_size() == 2)
            {
                node_reference[node3->input(1)] -= 1;
            }

            blob_names.erase(node->output(0));
            blob_names.erase(node2->output(0));

            node3->set_op_type("Reorg");
            node3->set_input(0, node->input(0));

            onnx::AttributeProto* attr_group = node3->add_attribute();
            attr_group->set_name("stride");
            attr_group->set_i(shape[3]);

            reduced_node_count += 2;
            i += 2;
        }
    }
}

static void fuse_expand_broadcast(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // Add/Sub/Mul/Div/Min/Max <= Expand - Add/Sub/Mul/Div/Min/Max
        if (node->op_type() == "Expand")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "Add" && node2->op_type() != "Sub" && node2->op_type() != "Mul" && node2->op_type() != "Div" && node2->op_type() != "Min" && node2->op_type() != "Max")
                continue;

            if (node2->input(1) != node->output(0) && node2->input(0) != node->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));

            node2->set_input(1, node->input(0));

            reduced_node_count += 1;
            i += 1;
        }
    }
}

static void fuse_lstm_gru_rnn(onnx::GraphProto* mutable_graph, std::map<std::string, onnx::TensorProto>& weights, std::map<std::string, int>& node_reference, std::set<std::string>& blob_names, int& reduced_node_count)
{
    int node_count = mutable_graph->node_size();
    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // LSTM(bi) <= LSTM(bi) - Transpose - Reshape - Transpose
        if (node->op_type() == "LSTM" || node->op_type() == "GRU" || node->op_type() == "RNN")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 2 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);
            onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 2);

            if (node2->op_type() != "Transpose" || node3->op_type() != "Reshape")
                continue;

            if (node_reference[node2->output(0)] != 1)
                continue;

            if (node2->input(0) != node->output(0) || node3->input(0) != node2->output(0))
                continue;

            std::string direction = get_node_attr_s(*node, "direction");
            if (direction != "bidirectional")
                continue;

            // 0 2 1 3
            std::vector<int> perm = get_node_attr_ai(*node2, "perm");
            if (perm.size() != 4)
                continue;

            if (perm[0] != 0 || perm[1] != 2 || perm[2] != 1 || perm[3] != 3)
                continue;

            std::vector<int> shape;
            if (node3->input_size() == 1)
            {
                shape = get_node_attr_ai(*node3, "shape");
            }
            else
            {
                // skip weight reshape
                if (weights.find(node3->input(1)) == weights.end())
                    continue;

                shape = get_node_attr_from_input_ai(weights[node3->input(1)]);
            }

            // 0 0 -1
            if (shape.size() != 3)
                continue;

            if (shape[0] != 0 || shape[1] != 0 || shape[2] != -1)
                continue;

            // reduce
            node2->set_op_type("noop_reducedncnn");
            node3->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;
            node_reference[node2->output(0)] -= 1;
            if (node3->input_size() == 2)
            {
                node_reference[node3->input(1)] -= 1;
            }

            blob_names.erase(node->output(0));
            if (node->output_size() > 1)
            {
                for (int j = 1; j < node->output_size(); j++)
                {
                    blob_names.erase(node->output(j));
                }
            }
            blob_names.erase(node2->output(0));

            node->clear_output();
            node->add_output(node3->output(0));

            reduced_node_count += 2;
            i += 2;

            if (i + 1 < node_count)
            {
                if (node_reference[node3->output(0)] != 1)
                    continue;

                onnx::NodeProto* node4 = mutable_graph->mutable_node(i + 1);

                if (node4->op_type() != "Transpose")
                    continue;

                if (node4->input(0) != node->output(0))
                    continue;

                // 1 0 2
                std::vector<int> perm4 = get_node_attr_ai(*node4, "perm");
                if (perm4.size() != 3)
                    continue;

                if (perm4[0] != 1 || perm4[1] != 0 || perm4[2] != 2)
                    continue;

                // reduce
                node4->set_op_type("noop_reducedncnn");

                node_reference[node->output(0)] -= 1;

                blob_names.erase(node->output(0));

                node->clear_output();
                node->add_output(node4->output(0));

                reduced_node_count += 1;
                i += 1;
            }
        }
    }

    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // LSTM(uni) <= LSTM(uni) - Squeeze - Transpose
        if (node->op_type() == "LSTM" || node->op_type() == "GRU" || node->op_type() == "RNN")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "Squeeze")
                continue;

            if (node2->input(0) != node->output(0))
                continue;

            std::string direction = get_node_attr_s(*node, "direction");
            if (direction == "bidirectional")
                continue;

            // 1
            std::vector<int> axes = get_node_attr_ai(*node2, "axes");
            if (axes.size() != 1)
                continue;

            if (axes[0] != 1)
                continue;

            // reduce
            node2->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));
            if (node->output_size() > 1)
            {
                for (int j = 1; j < node->output_size(); j++)
                {
                    blob_names.erase(node->output(j));
                }
            }

            node->clear_output();
            node->add_output(node2->output(0));

            reduced_node_count += 1;
            i += 1;

            if (i + 1 < node_count)
            {
                if (node_reference[node2->output(0)] != 1)
                    continue;

                onnx::NodeProto* node3 = mutable_graph->mutable_node(i + 1);

                if (node3->op_type() != "Transpose")
                    continue;

                if (node3->input(0) != node->output(0))
                    continue;

                // 1 0 2
                std::vector<int> perm4 = get_node_attr_ai(*node3, "perm");
                if (perm4.size() != 3)
                    continue;

                if (perm4[0] != 1 || perm4[1] != 0 || perm4[2] != 2)
                    continue;

                // reduce
                node3->set_op_type("noop_reducedncnn");

                node_reference[node->output(0)] -= 1;

                blob_names.erase(node->output(0));

                node->clear_output();
                node->add_output(node3->output(0));

                reduced_node_count += 1;
                i += 1;
            }
        }
    }

    for (int i = 0; i < node_count; i++)
    {
        onnx::NodeProto* node = mutable_graph->mutable_node(i);

        // LSTM <= Transpose - LSTM
        if (node->op_type() == "Transpose")
        {
            if (node_reference[node->output(0)] != 1)
                continue;

            // 1 0 2
            std::vector<int> perm = get_node_attr_ai(*node, "perm");
            if (perm.size() != 3)
                continue;

            if (perm[0] != 1 || perm[1] != 0 || perm[2] != 2)
                continue;

            if (i + 1 >= node_count)
                continue;

            onnx::NodeProto* node2 = mutable_graph->mutable_node(i + 1);

            if (node2->op_type() != "LSTM" || node->op_type() == "GRU" || node->op_type() == "RNN")
                continue;

            if (node2->input(0) != node->output(0))
                continue;

            // reduce
            node->set_op_type("noop_reducedncnn");

            node_reference[node->output(0)] -= 1;

            blob_names.erase(node->output(0));

            node2->set_input(0, node->input(0));

            reduced_node_count += 1;
            i += 1;
        }
    }
}

int main(int argc, char** argv)
{
    const char* onnxpb = argv[1];
    const char* ncnn_prototxt = argc >= 4 ? argv[2] : "ncnn.param";
    const char* ncnn_modelbin = argc >= 4 ? argv[3] : "ncnn.bin";

    onnx::ModelProto model;

    // load
    bool s1 = read_proto_from_binary(onnxpb, &model);
    if (!s1)
    {
        fprintf(stderr, "read_proto_from_binary failed\n");
        return -1;
    }

    FILE* pp = fopen(ncnn_prototxt, "wb");
    FILE* bp = fopen(ncnn_modelbin, "wb");

    // magic
    fprintf(pp, "7767517\n");

    const onnx::GraphProto& graph = model.graph();
    onnx::GraphProto* mutable_graph = model.mutable_graph();

    int node_count = graph.node_size();

    // node reference
    std::map<std::string, int> node_reference;

    // weight node and weight reshape node
    std::map<std::string, onnx::TensorProto> weights;

    for (int j = 0; j < graph.initializer_size(); j++)
    {
        const onnx::TensorProto& initializer = graph.initializer(j);

        //         fprintf(stderr, "weight = %s %d\n", initializer.name().c_str(), initializer.data_type());

        weights[initializer.name()] = initializer;
    }

    // global definition line
    // [layer count] [blob count]
    std::set<std::string> blob_names;
    for (int i = 0; i < node_count; i++)
    {
        const onnx::NodeProto& node = graph.node(i);

        const std::string& op = node.op_type();

        std::string name = node.name();
        if (name.empty())
        {
            name = node.output(0);
        }

        if (op == "Constant")
        {
            onnx::TensorProto tensor = get_node_attr_tensor(node, "value");
            weights[node.output(0)] = tensor;
        }

        for (int j = 0; j < (int)node.input_size(); j++)
        {
            const std::string& input_name = node.input(j);

            blob_names.insert(input_name);

            if (node_reference.find(input_name) == node_reference.end())
            {
                node_reference[input_name] = 1;
            }
            else
            {
                node_reference[input_name] = node_reference[input_name] + 1;
            }
        }

        if (op == "Dropout")
        {
            const std::string& output_name = node.output(0);
            blob_names.insert(output_name);
            node_reference[output_name] = 0;
            continue;
        }

        for (int j = 0; j < (int)node.output_size(); j++)
        {
            const std::string& output_name = node.output(j);

            blob_names.insert(output_name);

            node_reference[output_name] = 0;
        }
    }

    // include Input node
    int input_node_count = 0;
    for (int j = 0; j < graph.input_size(); j++)
    {
        const std::string& input_name = graph.input(j).name();

        // check weight
        if (weights.find(input_name) != weights.end())
            continue;

        blob_names.insert(input_name);

        input_node_count++;
    }

    //     for (auto a: node_reference)
    //     {
    //         fprintf(stderr, "a = %s %d\n", a.first.c_str(), a.second);
    //     }

    // op chain fusion
    int reduced_node_count = 0;
    fuse_weight_reshape(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_weight_transpose(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_shufflechannel(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_shufflechannel_split(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_hardsigmoid(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_hardswish(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_swish(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_batchnorm1d_squeeze_unsqueeze(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_unsqueeze_prelu(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_normalize(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_groupnorm(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_flatten(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_pixelshuffle(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_reorg(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_expand_broadcast(mutable_graph, weights, node_reference, blob_names, reduced_node_count);
    fuse_lstm_gru_rnn(mutable_graph, weights, node_reference, blob_names, reduced_node_count);

    // reduce common const weight node_reference
    for (int i = 0; i < node_count; i++)
    {
        const onnx::NodeProto& node = graph.node(i);

        const std::string& op = node.op_type();

        if (op == "Add" || op == "Sub" || op == "Mul" || op == "Div" || op == "Max" || op == "Min" || op == "Pow")
        {
            // binaryop with scalar
            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                node_reference[node.input(1)] -= 1;
            }
        }
        else if (op == "BatchNormalization")
        {
            node_reference[node.input(1)] -= 1;
            node_reference[node.input(2)] -= 1;
            node_reference[node.input(3)] -= 1;
            node_reference[node.input(4)] -= 1;
        }
        else if (op == "Clip")
        {
            if (node.input_size() == 3)
            {
                node_reference[node.input(1)] -= 1;
                node_reference[node.input(2)] -= 1;
            }
        }
        else if (op == "Conv")
        {
            node_reference[node.input(1)] -= 1;
            if (node.input_size() == 3)
            {
                node_reference[node.input(2)] -= 1;
            }
        }
        else if (op == "ConvTranspose")
        {
            node_reference[node.input(1)] -= 1;
            if (node.input_size() == 3)
            {
                node_reference[node.input(2)] -= 1;
            }
        }
        else if (op == "Gemm")
        {
            float alpha = get_node_attr_f(node, "alpha", 1.f);
            float beta = get_node_attr_f(node, "beta", 1.f);
            int transA = get_node_attr_i(node, "transA", 0);
            int transB = get_node_attr_i(node, "transB", 0);

            if (alpha == 1.f && beta == 1.f && transA == 0 && transB == 1)
            {
                // InnerProduct-like A * B + C
                node_reference[node.input(1)] -= 1;
                node_reference[node.input(2)] -= 1;
            }
        }
        else if (op == "GroupNorm")
        {
            int affine = get_node_attr_i(node, "affine", 1);
            if (affine)
            {
                node_reference[node.input(1)] -= 1;
                node_reference[node.input(2)] -= 1;
            }
        }
        else if (op == "GRU")
        {
            for (int j = 1; j < node.input_size(); j++)
            {
                node_reference[node.input(j)] -= 1;
            }
        }
        else if (op == "InstanceNormalization")
        {
            node_reference[node.input(1)] -= 1;
            node_reference[node.input(2)] -= 1;
        }
        else if (op == "LSTM")
        {
            for (int j = 1; j < node.input_size(); j++)
            {
                node_reference[node.input(j)] -= 1;
            }
        }
        else if (op == "MatMul")
        {
            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 2)
            {
                // InnerProduct
                node_reference[node.input(1)] -= 1;
            }
        }
        else if (op == "Pad")
        {
            if (node.input_size() >= 2)
            {
                node_reference[node.input(1)] -= 1;
            }
        }
        else if (op == "PRelu")
        {
            node_reference[node.input(1)] -= 1;
        }
        else if (op == "Reshape")
        {
            if (node.input_size() >= 2)
            {
                node_reference[node.input(1)] -= 1;
            }
        }
        else if (op == "Resize")
        {
            if (node.input_size() == 2)
            {
                // opset 10
                node_reference[node.input(1)] -= 1;
            }
            else
            {
                // opset 11+
                node_reference[node.input(1)] -= 1;
                node_reference[node.input(2)] -= 1;
                if (node.input_size() >= 4)
                {
                    node_reference[node.input(3)] -= 1;
                }
            }
        }
        else if (op == "RNN")
        {
            for (int j = 1; j < node.input_size(); j++)
            {
                node_reference[node.input(j)] -= 1;
            }
        }
        else if (op == "Slice")
        {
            if (node.input_size() >= 2)
            {
                node_reference[node.input(1)] -= 1;
                node_reference[node.input(2)] -= 1;
                if (node.input_size() >= 4)
                    node_reference[node.input(3)] -= 1;
                if (node.input_size() >= 5)
                    node_reference[node.input(4)] -= 1;
            }
        }
        else if (op == "Upsample")
        {
            if (node.input_size() >= 2)
            {
                node_reference[node.input(1)] -= 1;
            }
        }
        else if (op == "adaptive_avg_pool2d" || op == "adaptive_max_pool2d")
        {
            if (node.input_size() >= 2)
            {
                node_reference[node.input(1)] -= 1;
            }
        }
    }

    //         for (auto a: node_reference)
    //         {
    //             fprintf(stderr, "b = %s %d\n", a.first.c_str(), a.second);
    //         }

    // count all weight node with zero reference
    int zero_reference_weight_node_count = 0;
    for (std::map<std::string, onnx::TensorProto>::iterator it = weights.begin(); it != weights.end(); it++)
    {
        const std::string& input_name = it->first;

        int refcount = node_reference[input_name];
        if (refcount == 0)
            zero_reference_weight_node_count++;
    }

    // we always treat constant node as weight or binaryop_weights
    // do not count it twice for layer_count
    int constant_node_count_moved_to_weight = 0;
    for (int i = 0; i < node_count; i++)
    {
        const onnx::NodeProto& node = graph.node(i);

        const std::string& op = node.op_type();

        if (op == "Constant")
        {
            constant_node_count_moved_to_weight++;
        }
    }

    // some op may have anonymous input
    // LSTM sequence_lens
    blob_names.erase("");

    // remove node_reference entry with reference equals to one
    int split_layer_count = 0;
    int splitncnn_blob_count = 0;
    // split node reference
    std::map<std::string, int> split_node_reference;
    for (std::map<std::string, int>::iterator it = node_reference.begin(); it != node_reference.end(); it++)
    {
        if (it->second > 1)
        {
            split_layer_count++;
            splitncnn_blob_count += it->second;

            split_node_reference[it->first] = it->second;
        }
    }

    fprintf(pp, "%zu %zu\n", node_count - constant_node_count_moved_to_weight + weights.size() - zero_reference_weight_node_count - reduced_node_count + input_node_count + split_layer_count, blob_names.size() - zero_reference_weight_node_count + splitncnn_blob_count);

    int internal_split = 0;

    // place Input at the beginning
    for (int j = 0; j < graph.input_size(); j++)
    {
        const std::string& input_name = graph.input(j).name();

        // check weight
        if (weights.find(input_name) != weights.end())
            continue;

        fprintf(pp, "%-16s %-24s 0 1 %s\n", "Input", input_name.c_str(), input_name.c_str());

        int refcount = node_reference[input_name];
        if (refcount <= 1)
        {
            continue;
        }

        char splitname[256];
        sprintf(splitname, "splitncnn_input%d", j);
        fprintf(pp, "%-16s %-24s %d %d", "Split", splitname, 1, refcount);
        fprintf(pp, " %s", input_name.c_str());

        for (int k = 0; k < refcount; k++)
        {
            fprintf(pp, " %s_splitncnn_%d", input_name.c_str(), k);
        }
        fprintf(pp, "\n");
    }

    // place MemoryData next
    for (std::map<std::string, onnx::TensorProto>::iterator weight_it = weights.begin(); weight_it != weights.end(); weight_it++)
    {
        const std::string& input_name = weight_it->first;

        int refcount = node_reference[input_name];
        if (refcount == 0)
        {
            continue;
        }

        fprintf(pp, "%-16s %-24s 0 1 %s", "MemoryData", input_name.c_str(), input_name.c_str());

        const onnx::TensorProto& M = weights[input_name];

        if (M.dims_size() == 0)
        {
            fprintf(pp, " 0=%d", get_tensor_proto_data_size(M));
        }
        else if (M.dims_size() == 1)
        {
            fprintf(pp, " 0=%d", (int)M.dims(0));
        }
        else if (M.dims_size() == 2)
        {
            fprintf(pp, " 0=%d", (int)M.dims(1));
        }
        else if (M.dims_size() == 3)
        {
            fprintf(pp, " 0=%d", (int)M.dims(2));
            fprintf(pp, " 1=%d", (int)M.dims(1));
        }
        else if (M.dims_size() == 4)
        {
            fprintf(pp, " 0=%d", (int)M.dims(3));
            fprintf(pp, " 1=%d", (int)M.dims(2));
            fprintf(pp, " 2=%d", (int)M.dims(1));
        }

        fprintf(pp, "\n");

        fwrite_tensor_proto_data(M, bp);

        if (refcount <= 1)
        {
            continue;
        }

        char splitname[256];
        sprintf(splitname, "splitncnn_%d", internal_split);
        fprintf(pp, "%-16s %-24s %d %d", "Split", splitname, 1, refcount);

        fprintf(pp, " %s", input_name.c_str());

        for (int k = 0; k < refcount; k++)
        {
            fprintf(pp, " %s_splitncnn_%d", input_name.c_str(), k);
        }
        fprintf(pp, "\n");

        internal_split++;
    }

    for (int i = 0; i < node_count; i++)
    {
        const onnx::NodeProto& node = graph.node(i);

        const std::string& op = node.op_type();

        //         fprintf(stderr, "op = %s\n", op.c_str());

        if (op == "noop_reducedncnn")
        {
            continue;
        }

        std::string name = node.name();
        if (name.empty())
        {
            name = node.output(0);
        }

        int input_size = node.input_size();
        int output_size = node.output_size();

        for (int j = 0; j < (int)node.input_size(); j++)
        {
            const std::string& input_name = node.input(j);

            // check weight
            if (weights.find(input_name) != weights.end() && node_reference[input_name] == 0)
            {
                input_size--;
            }

            if (input_name.empty())
            {
                input_size--;
            }

            //             fprintf(stderr, "  input = %s\n", input_name.c_str());
        }
        /*
        for (int j=0; j<(int)node.output_size(); j++)
        {
            const std::string& output_name = node.output(j);
            fprintf(stderr, "  output = %s\n", output_name.c_str());
        }
        */

        if (op == "Abs")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Acos")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Add")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Asin")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Atan")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "AveragePool" || op == "MaxPool")
        {
            fprintf(pp, "%-16s", "Pooling");
        }
        else if (op == "BatchNormalization")
        {
            fprintf(pp, "%-16s", "BatchNorm");
        }
        else if (op == "Ceil")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Clip")
        {
            fprintf(pp, "%-16s", "Clip");
        }
        else if (op == "Concat")
        {
            fprintf(pp, "%-16s", "Concat");
        }
        else if (op == "Constant")
        {
            continue;
        }
        else if (op == "Conv")
        {
            int group = get_node_attr_i(node, "group", 1);
            if (group > 1)
            {
                fprintf(pp, "%-16s", "ConvolutionDepthWise");
            }
            else
            {
                fprintf(pp, "%-16s", "Convolution");
            }
        }
        else if (op == "ConvTranspose")
        {
            int group = get_node_attr_i(node, "group", 1);
            if (group > 1)
            {
                fprintf(pp, "%-16s", "DeconvolutionDepthWise");
            }
            else
            {
                fprintf(pp, "%-16s", "Deconvolution");
            }
        }
        else if (op == "Cos")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "DepthToSpace")
        {
            fprintf(pp, "%-16s", "PixelShuffle");
        }
        else if (op == "Div")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Dropout")
        {
            fprintf(pp, "%-16s", "Dropout");
            output_size = 1;
        }
        else if (op == "Elu")
        {
            fprintf(pp, "%-16s", "ELU");
        }
        else if (op == "Exp")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Flatten")
        {
            fprintf(pp, "%-16s", "Flatten");
        }
        else if (op == "Floor")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Gemm")
        {
            float alpha = get_node_attr_f(node, "alpha", 1.f);
            float beta = get_node_attr_f(node, "beta", 1.f);
            int transA = get_node_attr_i(node, "transA", 0);
            int transB = get_node_attr_i(node, "transB", 0);

            if (alpha == 1.f && beta == 1.f && transA == 0 && transB == 1)
            {
                // InnerProduct-like A * B + C
                fprintf(pp, "%-16s", "InnerProduct");
            }
            else
            {
                fprintf(pp, "%-16s", "Gemm");
            }
        }
        else if (op == "GlobalAveragePool")
        {
            fprintf(pp, "%-16s", "Pooling");
        }
        else if (op == "GlobalMaxPool")
        {
            fprintf(pp, "%-16s", "Pooling");
        }
        else if (op == "adaptive_avg_pool2d" || op == "adaptive_max_pool2d")
        {
            fprintf(pp, "%-16s", "Pooling");
        }
        else if (op == "GroupNorm")
        {
            fprintf(pp, "%-16s", "GroupNorm");
        }
        else if (op == "HardSigmoid")
        {
            fprintf(pp, "%-16s", "HardSigmoid");
        }
        else if (op == "HardSwish")
        {
            fprintf(pp, "%-16s", "HardSwish");
        }
        else if (op == "ImageScaler")
        {
            fprintf(pp, "%-16s", "Scale");
        }
        else if (op == "InstanceNormalization")
        {
            fprintf(pp, "%-16s", "InstanceNorm");
        }
        else if (op == "LeakyRelu")
        {
            fprintf(pp, "%-16s", "ReLU");
        }
        else if (op == "Log")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "LRN")
        {
            fprintf(pp, "%-16s", "LRN");
        }
        else if (op == "LSTM")
        {
            fprintf(pp, "%-16s", "LSTM");
        }
        else if (op == "MatMul")
        {
            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 2)
            {
                fprintf(pp, "%-16s", "InnerProduct");
            }
            else
            {
                fprintf(pp, "%-16s", "Gemm");
            }
        }
        else if (op == "Max")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Min")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Mul")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Neg")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Normalize")
        {
            fprintf(pp, "%-16s", "Normalize");
        }
        else if (op == "Pad")
        {
            fprintf(pp, "%-16s", "Padding");
        }
        else if (op == "PixelShuffle")
        {
            fprintf(pp, "%-16s", "PixelShuffle");
        }
        else if (op == "Pow")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "PRelu")
        {
            fprintf(pp, "%-16s", "PReLU");
        }
        else if (op == "Reciprocal")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "ReduceMax" || op == "ReduceMin" || op == "ReduceMean" || op == "ReduceProd" || op == "ReduceSum" || op == "ReduceSumSquare" || op == "ReduceL1" || op == "ReduceL2" || op == "ReduceLogSum" || op == "ReduceLogSumExp")
        {
            fprintf(pp, "%-16s", "Reduction");
        }
        else if (op == "Relu")
        {
            fprintf(pp, "%-16s", "ReLU");
        }
        else if (op == "Reorg")
        {
            fprintf(pp, "%-16s", "Reorg");
        }
        else if (op == "Reshape")
        {
            fprintf(pp, "%-16s", "Reshape");
        }
        else if (op == "ShuffleChannel")
        {
            fprintf(pp, "%-16s", "ShuffleChannel");
        }
        else if (op == "Sigmoid")
        {
            fprintf(pp, "%-16s", "Sigmoid");
        }
        else if (op == "Sin")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Slice")
        {
            fprintf(pp, "%-16s", "Crop");
        }
        else if (op == "Softmax")
        {
            fprintf(pp, "%-16s", "Softmax");
        }
        else if (op == "Softplus")
        {
            fprintf(pp, "%-16s", "Softplus");
        }
        else if (op == "Split")
        {
            fprintf(pp, "%-16s", "Slice");
        }
        else if (op == "Sqrt")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Squeeze")
        {
            fprintf(pp, "%-16s", "Squeeze");
        }
        else if (op == "Sub")
        {
            fprintf(pp, "%-16s", "BinaryOp");
        }
        else if (op == "Sum")
        {
            fprintf(pp, "%-16s", "Eltwise");
        }
        else if (op == "Swish")
        {
            fprintf(pp, "%-16s", "Swish");
        }
        else if (op == "Tan")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Tanh")
        {
            fprintf(pp, "%-16s", "UnaryOp");
        }
        else if (op == "Transpose")
        {
            fprintf(pp, "%-16s", "Permute");
        }
        else if (op == "Upsample" || op == "Resize")
        {
            fprintf(pp, "%-16s", "Interp");
        }
        else if (op == "Unsqueeze")
        {
            fprintf(pp, "%-16s", "ExpandDims");
        }
        else
        {
            // TODO
            fprintf(stderr, "%s not supported yet!\n", op.c_str());
            fprintf(pp, "%-16s", op.c_str());
        }

        fprintf(pp, " %-24s %d %d", name.c_str(), input_size, output_size);

        for (int j = 0; j < input_size; j++)
        {
            std::string input_name = node.input(j);

            // check weight
            if (weights.find(input_name) != weights.end() && node_reference[input_name] == 0)
            {
                continue;
            }

            if (split_node_reference.find(input_name) != split_node_reference.end())
            {
                int refidx = split_node_reference[input_name] - 1;
                split_node_reference[input_name] = refidx;

                char splitsuffix[256];
                sprintf(splitsuffix, "_splitncnn_%d", refidx);
                input_name = input_name + splitsuffix;
            }

            fprintf(pp, " %s", input_name.c_str());
        }

        for (int j = 0; j < output_size; j++)
        {
            const std::string& output_name = node.output(j);

            fprintf(pp, " %s", output_name.c_str());
        }

        if (op == "Abs")
        {
            int op_type = 0;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Acos")
        {
            int op_type = 13;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Add")
        {
            int op_type = 0;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Asin")
        {
            int op_type = 12;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Atan")
        {
            int op_type = 14;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "AveragePool" || op == "MaxPool")
        {
            std::string auto_pad = get_node_attr_s(node, "auto_pad");
            int ceil_mode = get_node_attr_i(node, "ceil_mode", 0);
            std::vector<int> kernel_shape = get_node_attr_ai(node, "kernel_shape");
            std::vector<int> strides = get_node_attr_ai(node, "strides");
            std::vector<int> pads = get_node_attr_ai(node, "pads");

            int pool = op == "AveragePool" ? 1 : 0;
            int pad_mode = 1;

            if (auto_pad == "SAME_UPPER")
            {
                pad_mode = 2;
            }
            else if (auto_pad == "SAME_LOWER")
            {
                pad_mode = 3;
            }

            if (ceil_mode == 1)
            {
                pad_mode = 0;
            }

            fprintf(pp, " 0=%d", pool);

            if (kernel_shape.size() == 1)
            {
                fprintf(pp, " 1=%d", kernel_shape[0]);
            }
            else if (kernel_shape.size() == 2)
            {
                fprintf(pp, " 1=%d", kernel_shape[1]);
                fprintf(pp, " 11=%d", kernel_shape[0]);
            }

            if (strides.size() == 1)
            {
                fprintf(pp, " 2=%d", strides[0]);
            }
            else if (strides.size() == 2)
            {
                fprintf(pp, " 2=%d", strides[1]);
                fprintf(pp, " 12=%d", strides[0]);
            }

            if (pads.size() == 1)
            {
                fprintf(pp, " 3=%d", pads[0]);
            }
            else if (pads.size() == 2)
            {
                fprintf(pp, " 3=%d", pads[1]);
                fprintf(pp, " 13=%d", pads[0]);
            }
            else if (pads.size() == 4)
            {
                fprintf(pp, " 3=%d", pads[1]);
                fprintf(pp, " 13=%d", pads[0]);
                fprintf(pp, " 14=%d", pads[3]);
                fprintf(pp, " 15=%d", pads[2]);
            }

            fprintf(pp, " 5=%d", pad_mode);

            if (op == "AveragePool")
            {
                int avgpool_count_include_pad = get_node_attr_i(node, "count_include_pad", 0);
                fprintf(pp, " 6=%d", avgpool_count_include_pad);
            }
        }
        else if (op == "BatchNormalization")
        {
            float epsilon = get_node_attr_f(node, "epsilon", 1e-5f);

            const onnx::TensorProto& scale = weights[node.input(1)];
            const onnx::TensorProto& B = weights[node.input(2)];
            const onnx::TensorProto& mean = weights[node.input(3)];
            const onnx::TensorProto& var = weights[node.input(4)];

            int channels = get_tensor_proto_data_size(scale);

            fprintf(pp, " 0=%d", channels);

            fwrite_tensor_proto_data(scale, bp);
            fwrite_tensor_proto_data(mean, bp);
            // apply epsilon to var
            {
                const float* v = var.has_raw_data() ? (const float*)var.raw_data().data() : var.float_data().data();

                for (int j = 0; j < channels; j++)
                {
                    float ve = v[j] + epsilon;
                    fwrite(&ve, sizeof(float), 1, bp);
                }
            }
            fwrite_tensor_proto_data(B, bp);
        }
        else if (op == "Ceil")
        {
            int op_type = 3;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Clip")
        {
            float min;
            float max;
            if (node.input_size() == 1)
            {
                min = get_node_attr_f(node, "min", -FLT_MAX);
                max = get_node_attr_f(node, "max", FLT_MAX);
            }
            else
            {
                const onnx::TensorProto& min_tp = weights[node.input(1)];
                const onnx::TensorProto& max_tp = weights[node.input(2)];

                min = get_node_attr_from_input_f(min_tp);
                max = get_node_attr_from_input_f(max_tp);
            }

            fprintf(pp, " 0=%e", min);
            fprintf(pp, " 1=%e", max);
        }
        else if (op == "Concat")
        {
            int axis = get_node_attr_i(node, "axis", 1);
            fprintf(pp, " 0=%d", axis - 1);
        }
        else if (op == "Constant")
        {
            // never reach here
        }
        else if (op == "Conv")
        {
            const onnx::TensorProto& W = weights[node.input(1)];

            int num_filter = W.dims(0);
            int has_bias = node.input_size() == 3 ? 1 : 0;

            std::string auto_pad = get_node_attr_s(node, "auto_pad");
            std::vector<int> kernel_shape = get_node_attr_ai(node, "kernel_shape");
            std::vector<int> dilations = get_node_attr_ai(node, "dilations");
            std::vector<int> strides = get_node_attr_ai(node, "strides");
            std::vector<int> pads = get_node_attr_ai(node, "pads");
            int group = get_node_attr_i(node, "group", 1);

            fprintf(pp, " 0=%d", num_filter);

            if (kernel_shape.size() == 1)
            {
                fprintf(pp, " 1=%d", kernel_shape[0]);
            }
            else if (kernel_shape.size() == 2)
            {
                fprintf(pp, " 1=%d", kernel_shape[1]);
                fprintf(pp, " 11=%d", kernel_shape[0]);
            }

            if (dilations.size() == 1)
            {
                fprintf(pp, " 2=%d", dilations[0]);
            }
            else if (dilations.size() == 2)
            {
                fprintf(pp, " 2=%d", dilations[1]);
                fprintf(pp, " 12=%d", dilations[0]);
            }

            if (strides.size() == 1)
            {
                fprintf(pp, " 3=%d", strides[0]);
            }
            else if (strides.size() == 2)
            {
                fprintf(pp, " 3=%d", strides[1]);
                fprintf(pp, " 13=%d", strides[0]);
            }

            if (auto_pad == "SAME_UPPER")
            {
                fprintf(pp, " 4=-233");
            }
            else if (auto_pad == "SAME_LOWER")
            {
                fprintf(pp, " 4=-234");
            }
            else
            {
                if (pads.size() == 1)
                {
                    fprintf(pp, " 4=%d", pads[0]);
                }
                else if (pads.size() == 2)
                {
                    fprintf(pp, " 4=%d", pads[1]);
                    fprintf(pp, " 14=%d", pads[0]);
                }
                else if (pads.size() == 4)
                {
                    fprintf(pp, " 4=%d", pads[1]);
                    fprintf(pp, " 14=%d", pads[0]);
                    fprintf(pp, " 15=%d", pads[3]);
                    fprintf(pp, " 16=%d", pads[2]);
                }
            }

            fprintf(pp, " 5=%d", has_bias);

            fprintf(pp, " 6=%d", get_tensor_proto_data_size(W));

            if (group > 1)
            {
                fprintf(pp, " 7=%d", group);
            }

            int quantize_tag = 0;
            fwrite(&quantize_tag, sizeof(int), 1, bp);

            fwrite_tensor_proto_data(W, bp);

            if (has_bias)
            {
                const onnx::TensorProto& B = weights[node.input(2)];
                fwrite_tensor_proto_data(B, bp);
            }
        }
        else if (op == "ConvTranspose")
        {
            const onnx::TensorProto& W = weights[node.input(1)];

            int has_bias = node.input_size() == 3 ? 1 : 0;

            std::string auto_pad = get_node_attr_s(node, "auto_pad");
            std::vector<int> kernel_shape = get_node_attr_ai(node, "kernel_shape");
            std::vector<int> dilations = get_node_attr_ai(node, "dilations");
            std::vector<int> strides = get_node_attr_ai(node, "strides");
            std::vector<int> output_padding = get_node_attr_ai(node, "output_padding");
            std::vector<int> output_shape = get_node_attr_ai(node, "output_shape");
            std::vector<int> pads = get_node_attr_ai(node, "pads");
            int group = get_node_attr_i(node, "group", 1);
            int num_filter = W.dims(1) * group;

            fprintf(pp, " 0=%d", num_filter);

            if (kernel_shape.size() == 1)
            {
                fprintf(pp, " 1=%d", kernel_shape[0]);
            }
            else if (kernel_shape.size() == 2)
            {
                fprintf(pp, " 1=%d", kernel_shape[1]);
                fprintf(pp, " 11=%d", kernel_shape[0]);
            }

            if (dilations.size() == 1)
            {
                fprintf(pp, " 2=%d", dilations[0]);
            }
            else if (dilations.size() == 2)
            {
                fprintf(pp, " 2=%d", dilations[1]);
                fprintf(pp, " 12=%d", dilations[0]);
            }

            if (strides.size() == 1)
            {
                fprintf(pp, " 3=%d", strides[0]);
            }
            else if (strides.size() == 2)
            {
                fprintf(pp, " 3=%d", strides[1]);
                fprintf(pp, " 13=%d", strides[0]);
            }

            if (auto_pad == "SAME_UPPER")
            {
                fprintf(pp, " 4=-233");
            }
            else if (auto_pad == "SAME_LOWER")
            {
                fprintf(pp, " 4=-234");
            }
            else
            {
                if (pads.size() == 1)
                {
                    fprintf(pp, " 4=%d", pads[0]);
                }
                else if (pads.size() == 2)
                {
                    fprintf(pp, " 4=%d", pads[1]);
                    fprintf(pp, " 14=%d", pads[0]);
                }
                else if (pads.size() == 4)
                {
                    fprintf(pp, " 4=%d", pads[1]);
                    fprintf(pp, " 14=%d", pads[0]);
                    fprintf(pp, " 15=%d", pads[3]);
                    fprintf(pp, " 16=%d", pads[2]);
                }
            }

            if (output_padding.size() == 1)
            {
                fprintf(pp, " 18=%d", output_padding[0]);
            }
            else if (output_padding.size() == 2)
            {
                fprintf(pp, " 18=%d", output_padding[1]);
                fprintf(pp, " 19=%d", output_padding[0]);
            }

            if (output_shape.size() == 1)
            {
                fprintf(pp, " 20=%d", output_shape[0]);
            }
            else if (output_shape.size() == 2)
            {
                fprintf(pp, " 20=%d", output_shape[1]);
                fprintf(pp, " 21=%d", output_shape[0]);
            }

            fprintf(pp, " 5=%d", has_bias);

            fprintf(pp, " 6=%d", get_tensor_proto_data_size(W));

            if (group > 1)
            {
                fprintf(pp, " 7=%d", group);
            }

            int quantize_tag = 0;
            fwrite(&quantize_tag, sizeof(int), 1, bp);

            int maxk = 0;
            if (kernel_shape.size() == 2)
            {
                maxk = kernel_shape[1] * kernel_shape[0];
            }
            else
            {
                maxk = kernel_shape[0] * kernel_shape[0];
            }
            int weight_data_size = get_tensor_proto_data_size(W);
            const float* weight_data = 0;
            if (W.has_raw_data())
            {
                weight_data = (const float*)W.raw_data().data();
            }
            else if (W.data_type() == 1)
            {
                weight_data = W.float_data().data();
            }
            for (int g = 0; g < group; g++)
            {
                // reorder weight from inch-outch to outch-inch
                int num_filter_g = num_filter / group;
                int num_input = weight_data_size / maxk / num_filter_g / group;
                const float* weight_data_ptr = weight_data + g * maxk * num_filter_g * num_input;
                for (int k = 0; k < num_filter_g; k++)
                {
                    for (int j = 0; j < num_input; j++)
                    {
                        fwrite(weight_data_ptr + (j * num_filter_g + k) * maxk, sizeof(float), maxk, bp);
                    }
                }
            }

            if (has_bias)
            {
                const onnx::TensorProto& B = weights[node.input(2)];
                fwrite_tensor_proto_data(B, bp);
            }
        }
        else if (op == "Cos")
        {
            int op_type = 10;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "DepthToSpace")
        {
            // pixelshuffle
            int scale_factor = get_node_attr_i(node, "blocksize", 1);
            std::string mode = get_node_attr_s(node, "mode");
            fprintf(pp, " 0=%d", scale_factor);
            if (mode == "CRD")
            {
                fprintf(pp, " 1=0");
            }
            else if (mode == "DCR")
            {
                fprintf(pp, " 1=1");
            }
        }
        else if (op == "Div")
        {
            int op_type = 3;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Dropout")
        {
            // no-op
        }
        else if (op == "Elu")
        {
            float alpha = get_node_attr_f(node, "alpha", 1.f);
            fprintf(pp, " 0=%e", alpha);
        }
        else if (op == "Exp")
        {
            int op_type = 7;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Flatten")
        {
            int axis = get_node_attr_i(node, "axis", 1);
            if (axis != 1)
            {
                fprintf(stderr, "Unsupported Flatten axis %d!\n", axis);
            }
        }
        else if (op == "Floor")
        {
            int op_type = 2;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Gemm")
        {
            float alpha = get_node_attr_f(node, "alpha", 1.f);
            float beta = get_node_attr_f(node, "beta", 1.f);
            int transA = get_node_attr_i(node, "transA", 0);
            int transB = get_node_attr_i(node, "transB", 0);

            if (alpha == 1.f && beta == 1.f && transA == 0 && transB == 1)
            {
                // InnerProduct-like A * B + C
                const onnx::TensorProto& B = weights[node.input(1)];
                const onnx::TensorProto& C = weights[node.input(2)];

                fprintf(pp, " 0=%d", get_tensor_proto_data_size(C));
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%d", get_tensor_proto_data_size(B));

                int quantize_tag = 0;
                fwrite(&quantize_tag, sizeof(int), 1, bp);

                fwrite_tensor_proto_data(B, bp);
                fwrite_tensor_proto_data(C, bp);
            }
            else
            {
                // gemm
                fprintf(pp, " 0=%e", alpha);
                fprintf(pp, " 1=%e", beta);
                fprintf(pp, " 2=%d", transA);
                fprintf(pp, " 3=%d", transB);
            }
        }
        else if (op == "GlobalAveragePool")
        {
            int pool = 1;
            int global_pool = 1;

            fprintf(pp, " 0=%d", pool);
            fprintf(pp, " 4=%d", global_pool);
        }
        else if (op == "GlobalMaxPool")
        {
            int pool = 0;
            int global_pool = 1;

            fprintf(pp, " 0=%d", pool);
            fprintf(pp, " 4=%d", global_pool);
        }
        else if (op == "adaptive_avg_pool2d" || op == "adaptive_max_pool2d")
        {
            int pool = 0;
            if (op == "adaptive_avg_pool2d")
            {
                pool = 1;
            }
            int adaptive_pooling = 1;
            const onnx::TensorProto& out_shape_tp = weights[node.input(1)];
            std::vector<int> out_shape = get_node_attr_from_input_ai(out_shape_tp);

            fprintf(pp, " 0=%d", pool);
            fprintf(pp, " 7=%d", adaptive_pooling);
            if (out_shape.size() == 1)
            {
                fprintf(pp, " 8=%d", out_shape[0]);
            }
            else if (out_shape.size() == 2)
            {
                // out_w
                fprintf(pp, " 8=%d", out_shape[1]);
                // out_h
                fprintf(pp, " 18=%d", out_shape[0]);
            }
        }
        else if (op == "GroupNorm")
        {
            int groups = get_node_attr_i(node, "groups", 1);
            int channels = get_node_attr_i(node, "channels", 1);
            float eps = get_node_attr_f(node, "epsilon", 1e-5f);
            int affine = get_node_attr_i(node, "affine", 1);

            fprintf(pp, " 0=%d", groups);
            fprintf(pp, " 1=%d", channels);
            fprintf(pp, " 2=%e", eps);
            fprintf(pp, " 3=%d", affine);
            if (affine)
            {
                const onnx::TensorProto& scale = weights[node.input(1)];
                const onnx::TensorProto& B = weights[node.input(2)];

                fwrite_tensor_proto_data(scale, bp);
                fwrite_tensor_proto_data(B, bp);
            }
        }
        else if (op == "HardSigmoid")
        {
            float alpha = get_node_attr_f(node, "alpha", 0.2f);
            float beta = get_node_attr_f(node, "beta", 0.5f);

            fprintf(pp, " 0=%e", alpha);
            fprintf(pp, " 1=%e", beta);
        }
        else if (op == "HardSwish")
        {
            float alpha = get_node_attr_f(node, "alpha", 0.2f);
            float beta = get_node_attr_f(node, "beta", 0.5f);

            fprintf(pp, " 0=%e", alpha);
            fprintf(pp, " 1=%e", beta);
        }
        else if (op == "ImageScaler")
        {
            std::vector<float> bias = get_node_attr_af(node, "bias");
            float scale = get_node_attr_f(node, "scale", 1.f);

            int channels = (int)bias.size();

            fprintf(pp, " 0=%d", channels);
            fprintf(pp, " 1=1");

            for (int j = 0; j < channels; j++)
            {
                fwrite(&scale, sizeof(float), 1, bp);
            }
            fwrite(&bias[0], sizeof(float), channels, bp);
        }
        else if (op == "InstanceNormalization")
        {
            float eps = get_node_attr_f(node, "epsilon", 1e-5f);

            // discard affine-less S=1 B=0
            std::vector<float> affine_S = get_node_attr_from_input_af(weights[node.input(1)]);
            std::vector<float> affine_B = get_node_attr_from_input_af(weights[node.input(2)]);
            int channels = (int)affine_S.size();
            int affine = 0;
            {
                for (int j = 0; j < channels; j++)
                {
                    if (affine_S[j] != 1.f || affine_B[j] != 0.f)
                    {
                        affine = 1;
                        break;
                    }
                }
            }

            fprintf(pp, " 0=%d", channels);
            fprintf(pp, " 1=%e", eps);
            fprintf(pp, " 2=%d", affine);
            if (affine)
            {
                const onnx::TensorProto& scale = weights[node.input(1)];
                const onnx::TensorProto& B = weights[node.input(2)];

                fwrite_tensor_proto_data(scale, bp);
                fwrite_tensor_proto_data(B, bp);
            }
        }
        else if (op == "LeakyRelu")
        {
            float alpha = get_node_attr_f(node, "alpha", 0.01f);

            fprintf(pp, " 0=%e", alpha);
        }
        else if (op == "Log")
        {
            int op_type = 8;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "LRN")
        {
            float alpha = get_node_attr_f(node, "alpha", 1.f);
            float beta = get_node_attr_f(node, "beta", 0.5f);
            float bias = get_node_attr_f(node, "bias", 1.f);
            int size = get_node_attr_i(node, "size", 1);

            int norm_region = 0;

            fprintf(pp, " 0=%d", norm_region);
            fprintf(pp, " 1=%d", size);
            fprintf(pp, " 2=%e", alpha);
            fprintf(pp, " 3=%e", beta);
            fprintf(pp, " 4=%e", bias);
        }
        else if (op == "LSTM")
        {
            const onnx::TensorProto& W = weights[node.input(1)];
            const onnx::TensorProto& R = weights[node.input(2)];
            const onnx::TensorProto& B = weights[node.input(3)];

            int hidden_size = get_node_attr_i(node, "hidden_size", 0);
            std::string direction = get_node_attr_s(node, "direction");

            int direction_type = 0;
            if (direction == "forward")
            {
                direction_type = 0;
            }
            else if (direction == "reverse")
            {
                direction_type = 1;
            }
            else if (direction == "bidirectional")
            {
                direction_type = 2;
            }

            int weight_data_size = get_tensor_proto_data_size(W);

            fprintf(pp, " 0=%d", hidden_size);
            fprintf(pp, " 1=%d", weight_data_size);
            fprintf(pp, " 2=%d", direction_type);

            int num_directions = direction_type == 2 ? 2 : 1;

            int quantize_tag = 0;

            // reorder num_directions-IOFG-hidden-size to num_directions-IFOG-hidden-size
            {
                fwrite(&quantize_tag, sizeof(int), 1, bp);

                int weight_data_size_g = get_tensor_proto_data_size(W) / 4 / num_directions;
                const float* wptr = W.has_raw_data() ? (const float*)W.raw_data().data() : W.float_data().data();

                const float* iptr = wptr;
                const float* optr = wptr + weight_data_size_g;
                const float* fptr = wptr + weight_data_size_g * 2;
                const float* gptr = wptr + weight_data_size_g * 3;
                fwrite(iptr, sizeof(float), weight_data_size_g, bp);
                fwrite(fptr, sizeof(float), weight_data_size_g, bp);
                fwrite(optr, sizeof(float), weight_data_size_g, bp);
                fwrite(gptr, sizeof(float), weight_data_size_g, bp);

                if (direction_type == 2)
                {
                    iptr += weight_data_size_g * 4;
                    optr += weight_data_size_g * 4;
                    fptr += weight_data_size_g * 4;
                    gptr += weight_data_size_g * 4;
                    fwrite(iptr, sizeof(float), weight_data_size_g, bp);
                    fwrite(fptr, sizeof(float), weight_data_size_g, bp);
                    fwrite(optr, sizeof(float), weight_data_size_g, bp);
                    fwrite(gptr, sizeof(float), weight_data_size_g, bp);
                }
            }

            // reduce xc and hc bias
            // reorder num_directions-IOFG-hidden to num_directions-IFOG-hidden
            {
                fwrite(&quantize_tag, sizeof(int), 1, bp);

                int bias_data_size_g = get_tensor_proto_data_size(B) / 2 / 4 / num_directions;
                const float* xcbptr = B.has_raw_data() ? (const float*)B.raw_data().data() : B.float_data().data();
                const float* xiptr = xcbptr;
                const float* xoptr = xcbptr + bias_data_size_g;
                const float* xfptr = xcbptr + bias_data_size_g * 2;
                const float* xgptr = xcbptr + bias_data_size_g * 3;
                const float* hiptr = xcbptr + bias_data_size_g * 4;
                const float* hoptr = xcbptr + bias_data_size_g * 5;
                const float* hfptr = xcbptr + bias_data_size_g * 6;
                const float* hgptr = xcbptr + bias_data_size_g * 7;

                for (int j = 0; j < bias_data_size_g; j++)
                {
                    float vb = xiptr[j] + hiptr[j];
                    fwrite(&vb, sizeof(float), 1, bp);
                }
                for (int j = 0; j < bias_data_size_g; j++)
                {
                    float vb = xfptr[j] + hfptr[j];
                    fwrite(&vb, sizeof(float), 1, bp);
                }
                for (int j = 0; j < bias_data_size_g; j++)
                {
                    float vb = xoptr[j] + hoptr[j];
                    fwrite(&vb, sizeof(float), 1, bp);
                }
                for (int j = 0; j < bias_data_size_g; j++)
                {
                    float vb = xgptr[j] + hgptr[j];
                    fwrite(&vb, sizeof(float), 1, bp);
                }

                if (direction_type == 2)
                {
                    xiptr += bias_data_size_g * 8;
                    xoptr += bias_data_size_g * 8;
                    xfptr += bias_data_size_g * 8;
                    xgptr += bias_data_size_g * 8;
                    hiptr += bias_data_size_g * 8;
                    hoptr += bias_data_size_g * 8;
                    hfptr += bias_data_size_g * 8;
                    hgptr += bias_data_size_g * 8;

                    for (int j = 0; j < bias_data_size_g; j++)
                    {
                        float vb = xiptr[j] + hiptr[j];
                        fwrite(&vb, sizeof(float), 1, bp);
                    }
                    for (int j = 0; j < bias_data_size_g; j++)
                    {
                        float vb = xfptr[j] + hfptr[j];
                        fwrite(&vb, sizeof(float), 1, bp);
                    }
                    for (int j = 0; j < bias_data_size_g; j++)
                    {
                        float vb = xoptr[j] + hoptr[j];
                        fwrite(&vb, sizeof(float), 1, bp);
                    }
                    for (int j = 0; j < bias_data_size_g; j++)
                    {
                        float vb = xgptr[j] + hgptr[j];
                        fwrite(&vb, sizeof(float), 1, bp);
                    }
                }
            }

            // reorder num_directions-IOFG-hidden-hidden to num_directions-IFOG-hidden-hidden
            {
                fwrite(&quantize_tag, sizeof(int), 1, bp);

                int weight_data_size_g = get_tensor_proto_data_size(R) / 4 / num_directions;
                const float* rptr = R.has_raw_data() ? (const float*)R.raw_data().data() : R.float_data().data();

                const float* iptr = rptr;
                const float* optr = rptr + weight_data_size_g;
                const float* fptr = rptr + weight_data_size_g * 2;
                const float* gptr = rptr + weight_data_size_g * 3;
                fwrite(iptr, sizeof(float), weight_data_size_g, bp);
                fwrite(fptr, sizeof(float), weight_data_size_g, bp);
                fwrite(optr, sizeof(float), weight_data_size_g, bp);
                fwrite(gptr, sizeof(float), weight_data_size_g, bp);

                if (direction_type == 2)
                {
                    iptr += weight_data_size_g * 4;
                    optr += weight_data_size_g * 4;
                    fptr += weight_data_size_g * 4;
                    gptr += weight_data_size_g * 4;
                    fwrite(iptr, sizeof(float), weight_data_size_g, bp);
                    fwrite(fptr, sizeof(float), weight_data_size_g, bp);
                    fwrite(optr, sizeof(float), weight_data_size_g, bp);
                    fwrite(gptr, sizeof(float), weight_data_size_g, bp);
                }
            }
        }
        else if (op == "MatMul")
        {
            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 2)
            {
                // InnerProduct
                const onnx::TensorProto& B = weights[node.input(1)];

                int weight_data_size = get_tensor_proto_data_size(B);

                int num_output = B.dims(B.dims_size() - 1);
                int num_input = weight_data_size / num_output;

                fprintf(pp, " 0=%d", num_output);
                fprintf(pp, " 1=0");
                fprintf(pp, " 2=%d", weight_data_size);

                int quantize_tag = 0;
                fwrite(&quantize_tag, sizeof(int), 1, bp);

                // reorder num_input-num_output to num_output-num_input
                {
                    const float* bptr = B.has_raw_data() ? (const float*)B.raw_data().data() : B.float_data().data();

                    for (int j = 0; j < num_output; j++)
                    {
                        for (int k = 0; k < num_input; k++)
                        {
                            float vb = bptr[k * num_output + j];
                            fwrite(&vb, sizeof(float), 1, bp);
                        }
                    }
                }

                // fwrite_tensor_proto_data(B, bp)
            }
            else
            {
                // default matrix multiplication
            }
        }
        else if (op == "Max")
        {
            int op_type = 4;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Min")
        {
            int op_type = 5;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Mul")
        {
            int op_type = 2;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Neg")
        {
            int op_type = 1;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Normalize")
        {
            float eps = get_node_attr_f(node, "eps", 0.f);
            int scale_data_size = 1;

            fprintf(pp, " 1=1"); // channel_shared
            fprintf(pp, " 2=%e", eps);
            fprintf(pp, " 3=%d", scale_data_size);
            fprintf(pp, " 9=1"); // TODO hardcode pytorch style

            const float scale_data[1] = {1.f};
            fwrite(scale_data, sizeof(float), 1, bp);
        }
        else if (op == "Pad")
        {
            std::string mode = get_node_attr_s(node, "mode");
            float value = get_node_attr_f(node, "value", 0.f);

            std::vector<int> pads;
            if (node.input_size() == 1)
            {
                pads = get_node_attr_ai(node, "pads");
            }
            else
            {
                pads = get_node_attr_from_input_ai(weights[node.input(1)]);
            }

            int type = 0;
            if (mode == "constant")
            {
                type = 0;
            }
            else if (mode == "edge")
            {
                type = 1;
            }
            else if (mode == "reflect")
            {
                type = 2;
            }

            int pad_size = (int)pads.size();
            int top = 0;
            int bottom = 0;
            int left = 0;
            int right = 0;
            int front = 0;
            int behind = 0;
            if (pad_size == 8)
            {
                //NCHW
                top = pads[2];
                bottom = pads[6];
                left = pads[3];
                right = pads[7];
                front = pads[1];
                behind = pads[5];
            }
            else if (pad_size == 6)
            {
                //NHW
                top = pads[1];
                bottom = pads[4];
                left = pads[2];
                right = pads[5];
            }
            else
            {
                //NW
                left = pads[1];
                right = pads[3];
            }

            fprintf(pp, " 0=%d", top);
            fprintf(pp, " 1=%d", bottom);
            fprintf(pp, " 2=%d", left);
            fprintf(pp, " 3=%d", right);
            fprintf(pp, " 4=%d", type);
            fprintf(pp, " 5=%e", value);
            fprintf(pp, " 7=%d", front);
            fprintf(pp, " 8=%d", behind);
        }
        else if (op == "Pow")
        {
            int op_type = 6;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "PixelShuffle")
        {
            int scale_factor = get_node_attr_i(node, "scale_factor", 1);
            fprintf(pp, " 0=%d", scale_factor);
        }
        else if (op == "PRelu")
        {
            const onnx::TensorProto& slope = weights[node.input(1)];

            int num_slope = get_tensor_proto_data_size(slope);

            fprintf(pp, " 0=%d", num_slope);

            fwrite_tensor_proto_data(slope, bp);
        }
        else if (op == "Reciprocal")
        {
            int op_type = 15;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "ReduceMax" || op == "ReduceMin" || op == "ReduceMean" || op == "ReduceProd" || op == "ReduceSum" || op == "ReduceSumSquare" || op == "ReduceL1" || op == "ReduceL2" || op == "ReduceLogSum" || op == "ReduceLogSumExp")
        {
            int op_type = -233;
            if (op == "ReduceSum")
                op_type = 0;
            else if (op == "ReduceSumSquare")
                op_type = 2;
            else if (op == "ReduceMean")
                op_type = 3;
            else if (op == "ReduceMax")
                op_type = 4;
            else if (op == "ReduceMin")
                op_type = 5;
            else if (op == "ReduceProd")
                op_type = 6;
            else if (op == "ReduceL1")
                op_type = 7;
            else if (op == "ReduceL2")
                op_type = 8;
            else if (op == "ReduceLogSum")
                op_type = 9;
            else if (op == "ReduceLogSumExp")
                op_type = 10;
            fprintf(pp, " 0=%d", op_type);

            std::vector<int> axes = get_node_attr_ai(node, "axes");
            int keepdims = get_node_attr_i(node, "keepdims", 1);

            if (axes.size() > 0)
            {
                // if axes set, reduce according to axes
                fprintf(pp, " 1=%d", 0);
                fprintf(pp, " -23303=%zu", axes.size());
                for (size_t j = 0; j < axes.size(); j++)
                {
                    if (axes[j] == 0 || axes[j] > 3 || axes[j] < -3)
                        fprintf(stderr, "Unsupported reduction axes !\n");
                    fprintf(pp, ",%d", axes[j]);
                }
            }
            else
            {
                // if axes not set, reduce all axes by default
                fprintf(pp, " 1=%d", 1);
            }
            fprintf(pp, " 4=%d", keepdims);
        }
        else if (op == "Reorg")
        {
            int stride = get_node_attr_i(node, "stride", 1);
            fprintf(pp, " 0=%d", stride);
        }
        else if (op == "Reshape")
        {
            std::vector<int> shape;

            if (node.input_size() == 1)
            {
                shape = get_node_attr_ai(node, "shape");
            }
            else
            {
                shape = get_node_attr_from_input_ai(weights[node.input(1)]);
            }

            if (shape.size() == 1)
            {
                fprintf(pp, " 0=%d", shape[0]); // should never reach here
            }
            else if (shape.size() == 2)
            {
                fprintf(pp, " 0=%d", shape[1]);
            }
            else if (shape.size() == 3)
            {
                fprintf(pp, " 0=%d", shape[2]);
                fprintf(pp, " 1=%d", shape[1]);
            }
            else if (shape.size() == 4)
            {
                fprintf(pp, " 0=%d", shape[3]);
                fprintf(pp, " 1=%d", shape[2]);
                fprintf(pp, " 2=%d", shape[1]);
            }
            else if (shape.size() == 5)
            {
                fprintf(pp, " 0=%d", shape[4] * shape[3]);
                fprintf(pp, " 1=%d", shape[2]);
                fprintf(pp, " 2=%d", shape[1]);
            }
        }
        else if (op == "Resize")
        {
            std::string mode = get_node_attr_s(node, "mode");
            std::string align = get_node_attr_s(node, "coordinate_transformation_mode");

            std::vector<float> scales;
            std::vector<int> sizes;
            if (node.input_size() == 2)
            {
                // opset 10
                scales = get_node_attr_from_input_af(weights[node.input(1)]);
            }
            else
            {
                // opset 11+
                scales = get_node_attr_from_input_af(weights[node.input(2)]);
                if (node.input_size() >= 4)
                {
                    sizes = get_node_attr_from_input_ai(weights[node.input(3)]);
                }
            }

            int resize_type = 1;
            if (mode == "nearest")
            {
                resize_type = 1;
            }
            else if (mode == "linear")
            {
                resize_type = 2;
            }
            else if (mode == "cubic")
            {
                resize_type = 3;
            }

            if (scales.empty() && sizes.empty())
            {
                fprintf(stderr, "Unsupported Resize scales and sizes are all empty!\n");
            }

            float h_scale = 1.f;
            float w_scale = 1.f;
            if (scales.size() == 2)
            {
                w_scale = scales[1];
            }
            else if (scales.size() == 3)
            {
                h_scale = scales[1];
                w_scale = scales[2];
            }
            else if (scales.size() == 4)
            {
                h_scale = scales[2];
                w_scale = scales[3];

                if (scales[1] != 1.f)
                    fprintf(stderr, "Unsupported Resize scales !\n");
            }

            int output_height = 0;
            int output_width = 0;
            if (sizes.size() == 2)
            {
                output_width = sizes[1];
            }
            else if (sizes.size() == 3)
            {
                output_height = sizes[1];
                output_width = sizes[2];
            }
            else if (sizes.size() == 4)
            {
                output_height = sizes[2];
                output_width = sizes[3];
            }

            int align_corner = 0;
            if (align == "align_corners")
            {
                align_corner = 1;
            }

            fprintf(pp, " 0=%d", resize_type);
            fprintf(pp, " 1=%e", h_scale);
            fprintf(pp, " 2=%e", w_scale);
            fprintf(pp, " 3=%d", output_height);
            fprintf(pp, " 4=%d", output_width);
            fprintf(pp, " 6=%d", align_corner);
        }
        else if (op == "ShuffleChannel")
        {
            int group = get_node_attr_i(node, "group", 1);
            int reverse = get_node_attr_i(node, "reverse", 0);
            fprintf(pp, " 0=%d", group);
            fprintf(pp, " 1=%d", reverse);
        }
        else if (op == "Sigmoid")
        {
        }
        else if (op == "Sin")
        {
            int op_type = 9;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Slice")
        {
            std::vector<int> starts;
            std::vector<int> ends;
            std::vector<int> axes;
            std::vector<int> steps;
            if (node.input_size() == 1)
            {
                starts = get_node_attr_ai(node, "starts");
                ends = get_node_attr_ai(node, "ends");
                axes = get_node_attr_ai(node, "axes");
                steps = get_node_attr_ai(node, "steps"); // TODO
            }
            else
            {
                starts = get_node_attr_from_input_ai(weights[node.input(1)]);
                ends = get_node_attr_from_input_ai(weights[node.input(2)]);
                if (node.input_size() >= 4)
                    axes = get_node_attr_from_input_ai(weights[node.input(3)]);
                if (node.input_size() >= 5)
                    steps = get_node_attr_from_input_ai(weights[node.input(4)]);
            }

            // assert step == 1
            for (int i = 0; i < (int)steps.size(); i++)
            {
                if (steps[i] != 1)
                    fprintf(stderr, "Unsupported slice step !\n");
            }

            // filter out N-dim axis
            if (!axes.empty())
            {
                for (int i = 0; i < (int)axes.size(); i++)
                {
                    int axis = axes[i];
                    if (axis == 0)
                    {
                        starts.erase(starts.begin() + i);
                        ends.erase(ends.begin() + i);
                        axes.erase(axes.begin() + i);
                        break;
                    }
                }
            }

            fprintf(pp, " -23309=%d", (int)starts.size());
            for (int i = 0; i < (int)starts.size(); i++)
            {
                fprintf(pp, ",%d", starts[i]);
            }
            fprintf(pp, " -23310=%d", (int)ends.size());
            for (int i = 0; i < (int)ends.size(); i++)
            {
                fprintf(pp, ",%d", ends[i]);
            }
            if (!axes.empty())
            {
                fprintf(pp, " -23311=%d", (int)axes.size());
                for (int i = 0; i < (int)axes.size(); i++)
                {
                    int axis = axes[i];
                    if (axis == 0 || axis > 3 || axis < -3)
                        fprintf(stderr, "Unsupported slice axes !\n");

                    if (axis > 0)
                        axis = axis - 1; // -1 for skip N-dim

                    fprintf(pp, ",%d", axis);
                }
            }
        }
        else if (op == "Softmax")
        {
            int axis = get_node_attr_i(node, "axis", 1);
            fprintf(pp, " 0=%d", axis - 1);
            fprintf(pp, " 1=1");
        }
        else if (op == "Split")
        {
            int axis = get_node_attr_i(node, "axis", 0);
            std::vector<int> split = get_node_attr_ai(node, "split");
            if (axis < 1)
                fprintf(stderr, "Unsupported split axis !\n");

            fprintf(pp, " -23300=%d", output_size);
            if (split.empty())
            {
                for (int i = 0; i < output_size; i++)
                {
                    fprintf(pp, ",-233");
                }
            }
            else
            {
                for (size_t i = 0; i < split.size() - 1; i++)
                {
                    fprintf(pp, ",%d", split[i]);
                }
                fprintf(pp, ",-233");
            }
            fprintf(pp, " 1=%d", axis - 1);
        }
        else if (op == "Sqrt")
        {
            int op_type = 5;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Squeeze")
        {
            std::vector<int> axes = get_node_attr_ai(node, "axes");

            if (axes.empty())
            {
                fprintf(pp, " 0=1");
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=1");
            }
            else
            {
                fprintf(pp, " -23303=%zu", axes.size());
                for (int i = 0; i < (int)axes.size(); i++)
                {
                    if (axes[i] == 0 || axes[i] > 3 || axes[i] < -3)
                        fprintf(stderr, "Unsupported squeeze axes !\n");
                    fprintf(pp, ",%d", axes[i]);
                }
            }
        }
        else if (op == "Sub")
        {
            int op_type = 1;
            fprintf(pp, " 0=%d", op_type);

            if (weights.find(node.input(1)) != weights.end() && weights[node.input(1)].dims_size() == 0)
            {
                float b = get_node_attr_from_input_f(weights[node.input(1)]);
                fprintf(pp, " 1=1");
                fprintf(pp, " 2=%e", b);
            }
        }
        else if (op == "Sum")
        {
            int op_type = 1;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Swish")
        {
        }
        else if (op == "Tan")
        {
            int op_type = 11;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Tanh")
        {
            int op_type = 16;
            fprintf(pp, " 0=%d", op_type);
        }
        else if (op == "Transpose")
        {
            std::vector<int> perm = get_node_attr_ai(node, "perm");

            if (perm.size() == 3)
            {
                if (perm[1] == 1 && perm[2] == 2)
                    fprintf(pp, " 0=0"); // w h
                else if (perm[1] == 2 && perm[2] == 1)
                    fprintf(pp, " 0=1"); // h w
            }
            else if (perm.size() == 4)
            {
                if (perm[1] == 1 && perm[2] == 2 && perm[3] == 3)
                    fprintf(pp, " 0=0"); // w h c
                else if (perm[1] == 1 && perm[2] == 3 && perm[3] == 2)
                    fprintf(pp, " 0=1"); // h w c
                else if (perm[1] == 2 && perm[2] == 1 && perm[3] == 3)
                    fprintf(pp, " 0=2"); // w c h
                else if (perm[1] == 2 && perm[2] == 3 && perm[3] == 1)
                    fprintf(pp, " 0=3"); // c w h
                else if (perm[1] == 3 && perm[2] == 1 && perm[3] == 2)
                    fprintf(pp, " 0=4"); // h c w
                else if (perm[1] == 3 && perm[2] == 2 && perm[3] == 1)
                    fprintf(pp, " 0=5"); // c h w
            }
            else if (perm.size() == 5)
            {
                if (perm[1] == 1 && perm[2] == 2 && perm[3] == 3 && perm[4] == 4)
                    fprintf(pp, " 0=0"); // wx h c
                else if (perm[1] == 1 && perm[2] == 3 && perm[3] == 4 && perm[4] == 2)
                    fprintf(pp, " 0=1"); // h wx c
                else if (perm[1] == 2 && perm[2] == 1 && perm[3] == 3 && perm[4] == 4)
                    fprintf(pp, " 0=2"); // wx c h
                else if (perm[1] == 2 && perm[2] == 3 && perm[3] == 4 && perm[4] == 1)
                    fprintf(pp, " 0=3"); // c wx h
                else if (perm[1] == 3 && perm[2] == 4 && perm[3] == 1 && perm[4] == 2)
                    fprintf(pp, " 0=4"); // h c wx
                else if (perm[1] == 3 && perm[2] == 4 && perm[3] == 2 && perm[4] == 1)
                    fprintf(pp, " 0=5"); // c h wx
                else
                    fprintf(stderr, "Unsupported transpose type !\n");
            }
        }
        else if (op == "Upsample")
        {
            std::string mode = get_node_attr_s(node, "mode");
            std::string align = get_node_attr_s(node, "coordinate_transformation_mode");

            std::vector<float> scales;

            if (node.input_size() == 1)
            {
                scales = get_node_attr_af(node, "scales");
            }
            else
            {
                scales = get_node_attr_from_input_af(weights[node.input(1)]);
            }

            int resize_type = 1;
            if (mode == "nearest")
            {
                resize_type = 1;
            }
            else if (mode == "bilinear" || mode == "linear")
            {
                resize_type = 2;
            }
            else if (mode == "trilinear")
            {
                fprintf(stderr, "Unsupported Upsample mode !\n");
            }

            float h_scale = 1.f;
            float w_scale = 1.f;
            if (scales.size() == 2)
            {
                w_scale = scales[1];
            }
            else if (scales.size() == 3)
            {
                h_scale = scales[1];
                w_scale = scales[2];
            }
            else if (scales.size() == 4)
            {
                h_scale = scales[2];
                w_scale = scales[3];

                if (scales[1] != 1.f)
                    fprintf(stderr, "Unsupported Upsample scales !\n");
            }
            else
            {
                fprintf(stderr, "Unsupported Upsample scales !\n");
            }

            int align_corner = 0;
            if (align == "align_corners")
            {
                align_corner = 1;
            }

            fprintf(pp, " 0=%d", resize_type);
            fprintf(pp, " 1=%e", h_scale);
            fprintf(pp, " 2=%e", w_scale);
            fprintf(pp, " 6=%d", align_corner);
        }
        else if (op == "Unsqueeze")
        {
            std::vector<int> axes = get_node_attr_ai(node, "axes");

            fprintf(pp, " -23303=%zu", axes.size());
            for (int i = 0; i < (int)axes.size(); i++)
            {
                if (axes[i] == 0 || axes[i] > 4 || axes[i] < -4)
                    fprintf(stderr, "Unsupported unsqueeze axes !\n");
                fprintf(pp, ",%d", axes[i]);
            }
        }
        else
        {
            // TODO op specific param
            for (int j = 0; j < node.attribute_size(); j++)
            {
                const onnx::AttributeProto& attr = node.attribute(j);
                if (attr.type() == 1)
                {
                    fprintf(stderr, "  # %s=%g\n", attr.name().c_str(), attr.f());
                }
                else if (attr.type() == 2)
                {
                    fprintf(stderr, "  # %s=%lld\n", attr.name().c_str(), (long long)attr.i());
                }
                else if (attr.type() == 3)
                {
                    fprintf(stderr, "  # %s=%s\n", attr.name().c_str(), attr.s().c_str());
                }
                else
                {
                    fprintf(stderr, "  # %s %d\n", attr.name().c_str(), attr.type());
                }
            }
        }

        fprintf(pp, "\n");

        for (int j = 0; j < output_size; j++)
        {
            const std::string& output_name = node.output(j);
            if (node_reference.find(output_name) != node_reference.end())
            {
                int refcount = node_reference[output_name];
                if (refcount > 1)
                {
                    char splitname[256];
                    sprintf(splitname, "splitncnn_%d", internal_split);
                    fprintf(pp, "%-16s %-24s %d %d", "Split", splitname, 1, refcount);

                    fprintf(pp, " %s", output_name.c_str());

                    for (int k = 0; k < refcount; k++)
                    {
                        fprintf(pp, " %s_splitncnn_%d", output_name.c_str(), k);
                    }
                    fprintf(pp, "\n");

                    internal_split++;
                }
            }
        }
    }

    fclose(pp);
    fclose(bp);

    return 0;
}
