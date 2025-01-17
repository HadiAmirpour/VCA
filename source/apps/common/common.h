/* Copyright (C) 2022 Christian Doppler Laboratory ATHENA
 *
 * Authors: Christian Feldmann <christian.feldmann@bitmovin.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *****************************************************************************/

#pragma once

#include <lib/vcaLib.h>

#include <string>
#include <vector>

#pragma once

namespace vca {

class FrameWithData
{
public:
    FrameWithData() = delete;
    FrameWithData(const vca_frame_info &frameInfo);
    ~FrameWithData() = default;

    uint8_t *getData() const
    {
        return (uint8_t *) (this->data.data());
    }
    size_t getFrameSize() const
    {
        return this->data.size();
    }
    vca_frame *getFrame()
    {
        return &this->vcaFrame;
    }

private:
    std::vector<uint8_t> data;
    vca_frame vcaFrame;
};

void vca_log(LogLevel level, std::string error);

} // namespace vca