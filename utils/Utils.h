/*
 * Copyright (c) 2016, 2017 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __UTILS_UTILS_H__
#define __UTILS_UTILS_H__

#include "arm_compute/core/Helpers.h"
#include "arm_compute/core/ITensor.h"
#include "arm_compute/core/Types.h"
#include "arm_compute/core/Validate.h"
#include "arm_compute/core/Window.h"
#include "arm_compute/runtime/Tensor.h"
#include "support/ToolchainSupport.h"

#ifdef ARM_COMPUTE_CL
#include "arm_compute/core/CL/OpenCL.h"
#include "arm_compute/runtime/CL/CLTensor.h"
#endif /* ARM_COMPUTE_CL */

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace arm_compute
{
namespace utils
{
/** Signature of an example to run
 *
 * @param[in] argc Number of command line arguments
 * @param[in] argv Command line arguments
 */
using example = void(int argc, const char **argv);

/** Run an example and handle the potential exceptions it throws
 *
 * @param[in] argc Number of command line arguments
 * @param[in] argv Command line arguments
 * @param[in] func Pointer to the function containing the code to run
 */
int run_example(int argc, const char **argv, example &func);

/** Draw a RGB rectangular window for the detected object
 *
 * @param[in, out] tensor Input tensor where the rectangle will be drawn on. Format supported: RGB888
 * @param[in]      rect   Geometry of the rectangular window
 * @param[in]      r      Red colour to use
 * @param[in]      g      Green colour to use
 * @param[in]      b      Blue colour to use
 */
void draw_detection_rectangle(arm_compute::ITensor *tensor, const arm_compute::DetectionWindow &rect, uint8_t r, uint8_t g, uint8_t b);

/** Parse the ppm header from an input file stream. At the end of the execution,
 *  the file position pointer will be located at the first pixel stored in the ppm file
 *
 * @param[in] fs Input file stream to parse
 *
 * @return The width, height and max value stored in the header of the PPM file
 */
std::tuple<unsigned int, unsigned int, int> parse_ppm_header(std::ifstream &fs);

/** Maps a tensor if needed
 *
 * @param[in] tensor   Tensor to be mapped
 * @param[in] blocking Specified if map is blocking or not
 */
template <typename T>
void map(T &tensor, bool blocking)
{
    ARM_COMPUTE_UNUSED(tensor);
    ARM_COMPUTE_UNUSED(blocking);
}

/** Unmaps a tensor if needed
 *
 * @param tensor  Tensor to be unmapped
 */
template <typename T>
void unmap(T &tensor)
{
    ARM_COMPUTE_UNUSED(tensor);
}

#ifdef ARM_COMPUTE_CL
/** Maps a tensor if needed
 *
 * @param[in] tensor   Tensor to be mapped
 * @param[in] blocking Specified if map is blocking or not
 */
void map(CLTensor &tensor, bool blocking)
{
    tensor.map(blocking);
}

/** Unmaps a tensor if needed
 *
 * @param tensor  Tensor to be unmapped
 */
void unmap(CLTensor &tensor)
{
    tensor.unmap();
}
#endif /* ARM_COMPUTE_CL */

/** Class to load the content of a PPM file into an Image
 */
class PPMLoader
{
public:
    PPMLoader()
        : _fs(), _width(0), _height(0)
    {
    }
    /** Open a PPM file and reads its metadata (Width, height)
     *
     * @param[in] ppm_filename File to open
     */
    void open(const std::string &ppm_filename)
    {
        ARM_COMPUTE_ERROR_ON(is_open());
        try
        {
            _fs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            _fs.open(ppm_filename, std::ios::in | std::ios::binary);

            unsigned int max_val = 0;
            std::tie(_width, _height, max_val) = parse_ppm_header(_fs);

            ARM_COMPUTE_ERROR_ON_MSG(max_val >= 256, "2 bytes per colour channel not supported in file %s", ppm_filename.c_str());
        }
        catch(const std::ifstream::failure &e)
        {
            ARM_COMPUTE_ERROR("Accessing %s: %s", ppm_filename.c_str(), e.what());
        }
    }
    /** Return true if a PPM file is currently open
         */
    bool is_open()
    {
        return _fs.is_open();
    }

    /** Initialise an image's metadata with the dimensions of the PPM file currently open
     *
     * @param[out] image  Image to initialise
     * @param[in]  format Format to use for the image (Must be RGB888 or U8)
     */
    template <typename T>
    void init_image(T &image, arm_compute::Format format)
    {
        ARM_COMPUTE_ERROR_ON(!is_open());
        ARM_COMPUTE_ERROR_ON(format != arm_compute::Format::RGB888 && format != arm_compute::Format::U8);

        // Use the size of the input PPM image
        arm_compute::TensorInfo image_info(_width, _height, format);
        image.allocator()->init(image_info);
    }

    /** Fill an image with the content of the currently open PPM file.
     *
     * @note If the image is a CLImage, the function maps and unmaps the image
     *
     * @param[in,out] image Image to fill (Must be allocated, and of matching dimensions with the opened PPM).
     */
    template <typename T>
    void fill_image(T &image)
    {
        ARM_COMPUTE_ERROR_ON(!is_open());
        ARM_COMPUTE_ERROR_ON(image.info()->dimension(0) != _width || image.info()->dimension(1) != _height);
        ARM_COMPUTE_ERROR_ON_FORMAT_NOT_IN(&image, arm_compute::Format::U8, arm_compute::Format::RGB888);
        try
        {
            // Map buffer if creating a CLTensor
            map(image, true);

            // Check if the file is large enough to fill the image
            const size_t current_position = _fs.tellg();
            _fs.seekg(0, std::ios_base::end);
            const size_t end_position = _fs.tellg();
            _fs.seekg(current_position, std::ios_base::beg);

            ARM_COMPUTE_ERROR_ON_MSG((end_position - current_position) < image.info()->tensor_shape().total_size() * image.info()->element_size(),
                                     "Not enough data in file");
            ARM_COMPUTE_UNUSED(end_position);

            switch(image.info()->format())
            {
                case arm_compute::Format::U8:
                {
                    // We need to convert the data from RGB to grayscale:
                    // Iterate through every pixel of the image
                    arm_compute::Window window;
                    window.set(arm_compute::Window::DimX, arm_compute::Window::Dimension(0, _width, 1));
                    window.set(arm_compute::Window::DimY, arm_compute::Window::Dimension(0, _height, 1));

                    arm_compute::Iterator out(&image, window);

                    unsigned char red   = 0;
                    unsigned char green = 0;
                    unsigned char blue  = 0;

                    arm_compute::execute_window_loop(window, [&](const arm_compute::Coordinates & id)
                    {
                        red   = _fs.get();
                        green = _fs.get();
                        blue  = _fs.get();

                        *out.ptr() = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
                    },
                    out);

                    break;
                }
                case arm_compute::Format::RGB888:
                {
                    // There is no format conversion needed: we can simply copy the content of the input file to the image one row at the time.
                    // Create a vertical window to iterate through the image's rows:
                    arm_compute::Window window;
                    window.set(arm_compute::Window::DimY, arm_compute::Window::Dimension(0, _height, 1));

                    arm_compute::Iterator out(&image, window);

                    arm_compute::execute_window_loop(window, [&](const arm_compute::Coordinates & id)
                    {
                        // Copy one row from the input file to the current row of the image:
                        _fs.read(reinterpret_cast<std::fstream::char_type *>(out.ptr()), _width * image.info()->element_size());
                    },
                    out);

                    break;
                }
                default:
                    ARM_COMPUTE_ERROR("Unsupported format");
            }

            // Unmap buffer if creating a CLTensor
            unmap(image);
        }
        catch(const std::ifstream::failure &e)
        {
            ARM_COMPUTE_ERROR("Loading PPM file: %s", e.what());
        }
    }

private:
    std::ifstream _fs;
    unsigned int  _width, _height;
};

/** Template helper function to save a tensor image to a PPM file.
 *
 * @note Only U8 and RGB888 formats supported.
 * @note Only works with 2D tensors.
 * @note If the input tensor is a CLTensor, the function maps and unmaps the image
 *
 * @param[in] tensor       The tensor to save as PPM file
 * @param[in] ppm_filename Filename of the file to create.
 */
template <typename T>
void save_to_ppm(T &tensor, const std::string &ppm_filename)
{
    ARM_COMPUTE_ERROR_ON_FORMAT_NOT_IN(&tensor, arm_compute::Format::RGB888, arm_compute::Format::U8);
    ARM_COMPUTE_ERROR_ON(tensor.info()->num_dimensions() > 2);

    std::ofstream fs;

    try
    {
        fs.exceptions(std::ofstream::failbit | std::ofstream::badbit | std::ofstream::eofbit);
        fs.open(ppm_filename, std::ios::out | std::ios::binary);

        const unsigned int width  = tensor.info()->tensor_shape()[0];
        const unsigned int height = tensor.info()->tensor_shape()[1];

        fs << "P6\n"
           << width << " " << height << " 255\n";

        // Map buffer if creating a CLTensor
        map(tensor, true);

        switch(tensor.info()->format())
        {
            case arm_compute::Format::U8:
            {
                arm_compute::Window window;
                window.set(arm_compute::Window::DimX, arm_compute::Window::Dimension(0, width, 1));
                window.set(arm_compute::Window::DimY, arm_compute::Window::Dimension(0, height, 1));

                arm_compute::Iterator in(&tensor, window);

                arm_compute::execute_window_loop(window, [&](const arm_compute::Coordinates & id)
                {
                    const unsigned char value = *in.ptr();

                    fs << value << value << value;
                },
                in);

                break;
            }
            case arm_compute::Format::RGB888:
            {
                arm_compute::Window window;
                window.set(arm_compute::Window::DimX, arm_compute::Window::Dimension(0, width, width));
                window.set(arm_compute::Window::DimY, arm_compute::Window::Dimension(0, height, 1));

                arm_compute::Iterator in(&tensor, window);

                arm_compute::execute_window_loop(window, [&](const arm_compute::Coordinates & id)
                {
                    fs.write(reinterpret_cast<std::fstream::char_type *>(in.ptr()), width * tensor.info()->element_size());
                },
                in);

                break;
            }
            default:
                ARM_COMPUTE_ERROR("Unsupported format");
        }

        // Unmap buffer if creating a CLTensor
        unmap(tensor);
    }
    catch(const std::ofstream::failure &e)
    {
        ARM_COMPUTE_ERROR("Writing %s: (%s)", ppm_filename.c_str(), e.what());
    }
}

/** Load the tensor with pre-trained data from a binary file
 *
 * @param[in] tensor   The tensor to be filled. Data type supported: F32.
 * @param[in] filename Filename of the binary file to load from.
 */
template <typename T>
void load_trained_data(T &tensor, const std::string &filename)
{
    ARM_COMPUTE_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(&tensor, 1, DataType::F32);

    std::ifstream fs;

    try
    {
        fs.exceptions(std::ofstream::failbit | std::ofstream::badbit | std::ofstream::eofbit);
        // Open file
        fs.open(filename, std::ios::in | std::ios::binary);

        if(!fs.good())
        {
            throw std::runtime_error("Could not load binary data: " + filename);
        }

        // Map buffer if creating a CLTensor
        map(tensor, true);

        Window window;

        window.set(arm_compute::Window::DimX, arm_compute::Window::Dimension(0, 1, 1));

        for(unsigned int d = 1; d < tensor.info()->num_dimensions(); ++d)
        {
            window.set(d, Window::Dimension(0, tensor.info()->tensor_shape()[d], 1));
        }

        arm_compute::Iterator in(&tensor, window);

        execute_window_loop(window, [&](const Coordinates & id)
        {
            fs.read(reinterpret_cast<std::fstream::char_type *>(in.ptr()), tensor.info()->tensor_shape()[0] * tensor.info()->element_size());
        },
        in);

#ifdef ARM_COMPUTE_CL
        // Unmap buffer if creating a CLTensor
        unmap(tensor);
#endif /* ARM_COMPUTE_CL */
    }
    catch(const std::ofstream::failure &e)
    {
        ARM_COMPUTE_ERROR("Writing %s: (%s)", filename.c_str(), e.what());
    }
}

/** Obtain numpy type string from DataType.
 *
 * @param[in] data_type Data type.
 *
 * @return numpy type string.
 */
inline std::string get_typestring(DataType data_type)
{
    // Check endianness
    const unsigned int i = 1;
    const char        *c = reinterpret_cast<const char *>(&i);
    std::string        endianness;
    if(*c == 1)
    {
        endianness = std::string("<");
    }
    else
    {
        endianness = std::string(">");
    }
    const std::string no_endianness("|");

    switch(data_type)
    {
        case DataType::U8:
            return no_endianness + "u" + support::cpp11::to_string(sizeof(uint8_t));
        case DataType::S8:
            return no_endianness + "i" + support::cpp11::to_string(sizeof(int8_t));
        case DataType::U16:
            return endianness + "u" + support::cpp11::to_string(sizeof(uint16_t));
        case DataType::S16:
            return endianness + "i" + support::cpp11::to_string(sizeof(int16_t));
        case DataType::U32:
            return endianness + "u" + support::cpp11::to_string(sizeof(uint32_t));
        case DataType::S32:
            return endianness + "i" + support::cpp11::to_string(sizeof(int32_t));
        case DataType::U64:
            return endianness + "u" + support::cpp11::to_string(sizeof(uint64_t));
        case DataType::S64:
            return endianness + "i" + support::cpp11::to_string(sizeof(int64_t));
        case DataType::F32:
            return endianness + "f" + support::cpp11::to_string(sizeof(float));
        case DataType::F64:
            return endianness + "f" + support::cpp11::to_string(sizeof(double));
        case DataType::SIZET:
            return endianness + "u" + support::cpp11::to_string(sizeof(size_t));
        default:
            ARM_COMPUTE_ERROR("NOT SUPPORTED!");
    }
}
} // namespace utils
} // namespace arm_compute
#endif /* __UTILS_UTILS_H__*/
