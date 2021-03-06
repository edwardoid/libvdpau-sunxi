/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include "vdpau_private.h"
#include "ve.h"

static int mpeg_find_startcode(const uint8_t *data, int len)
{
	int pos = 0;
	while (pos < len)
	{
		int zeros = 0;
		for ( ; pos < len; pos++)
		{
			if (data[pos] == 0x00)
				++zeros;
			else if (data[pos] == 0x01 && zeros >= 2)
			{
				++pos;
				break;
			}
			else
				zeros = 0;
		}

		uint8_t marker = data[pos++];

		if (marker >= 0x01 && marker <= 0xaf)
			return pos - 4;
	}
	return 0;
}

int mpeg12_decode(decoder_ctx_t *decoder, VdpPictureInfoMPEG1Or2 const *info, const int len, video_surface_ctx_t *output)
{
	int start_offset = mpeg_find_startcode(decoder->data, len);

	int i;
	void *ve_regs = ve_get_regs();

	// activate MPEG engine
	writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x0, ve_regs + VE_CTRL);

	// set quantisation tables
	for (i = 0; i < 64; ++i)
		writel((uint32_t)(64 + i) << 8 | info->intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);
	for (i = 0; i < 64; ++i)
		writel((uint32_t)(i) << 8 | info->non_intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);

	// set size
	uint16_t width = (decoder->width + 15) / 16;
	uint16_t height = (decoder->height + 15) / 16;
	writel((width << 8) | height, ve_regs + VE_MPEG_SIZE);
	writel(((width * 16) << 16) | (height * 16), ve_regs + VE_MPEG_FRAME_SIZE);

	// set picture header
	uint32_t pic_header = 0;
	pic_header |= ((info->picture_coding_type & 0xf) << 28);
	pic_header |= ((info->f_code[0][0] & 0xf) << 24);
	pic_header |= ((info->f_code[0][1] & 0xf) << 20);
	pic_header |= ((info->f_code[1][0] & 0xf) << 16);
	pic_header |= ((info->f_code[1][1] & 0xf) << 12);
	pic_header |= ((info->intra_dc_precision & 0x3) << 10);
	pic_header |= ((info->picture_structure & 0x3) << 8);
	pic_header |= ((info->top_field_first & 0x1) << 7);
	pic_header |= ((info->frame_pred_frame_dct & 0x1) << 6);
	pic_header |= ((info->concealment_motion_vectors & 0x1) << 5);
	pic_header |= ((info->q_scale_type & 0x1) << 4);
	pic_header |= ((info->intra_vlc_format & 0x1) << 3);
	pic_header |= ((info->alternate_scan & 0x1) << 2);
	pic_header |= ((info->full_pel_forward_vector & 0x1) << 1);
	pic_header |= ((info->full_pel_backward_vector & 0x1) << 0);
	if (decoder->profile == VDP_DECODER_PROFILE_MPEG1)
		pic_header |= 0x000003c0;
	writel(pic_header, ve_regs + VE_MPEG_PIC_HDR);

	// ??
	writel(0x800001b8, ve_regs + VE_MPEG_CTRL);

	// set forward/backward predicion buffers
	if (info->forward_reference != VDP_INVALID_HANDLE)
	{
		video_surface_ctx_t *forward = handle_get(info->forward_reference);
		writel(ve_virt2phys(forward->data), ve_regs + VE_MPEG_FWD_LUMA);
		writel(ve_virt2phys(forward->data + forward->plane_size), ve_regs + VE_MPEG_FWD_CHROMA);
	}
	if (info->backward_reference != VDP_INVALID_HANDLE)
	{
		video_surface_ctx_t *backward = handle_get(info->backward_reference);
		writel(ve_virt2phys(backward->data), ve_regs + VE_MPEG_BACK_LUMA);
		writel(ve_virt2phys(backward->data + backward->plane_size), ve_regs + VE_MPEG_BACK_CHROMA);
	}

	// set output buffers (Luma / Croma)
	writel(ve_virt2phys(output->data), ve_regs + VE_MPEG_REC_LUMA);
	writel(ve_virt2phys(output->data + output->plane_size), ve_regs + VE_MPEG_REC_CHROMA);
	writel(ve_virt2phys(output->data), ve_regs + VE_MPEG_ROT_LUMA);
	writel(ve_virt2phys(output->data + output->plane_size), ve_regs + VE_MPEG_ROT_CHROMA);

	// set input offset in bits
	writel(start_offset * 8, ve_regs + VE_MPEG_VLD_OFFSET);

	// set input length in bits
	writel((len - start_offset) * 8, ve_regs + VE_MPEG_VLD_LEN);

	// input end
	uint32_t input_addr = ve_virt2phys(decoder->data);
	writel(input_addr + VBV_SIZE - 1, ve_regs + VE_MPEG_VLD_END);

	// set input buffer
	writel((input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28), ve_regs + VE_MPEG_VLD_ADDR);

	// trigger
	writel((((decoder->profile == VDP_DECODER_PROFILE_MPEG1) ? 1 : 2) << 24) | 0x8000000f, ve_regs + VE_MPEG_TRIGGER);

	// wait for interrupt
	ve_wait(1);

	// clean interrupt flag
	writel(0x0000c00f, ve_regs + VE_MPEG_STATUS);

	// stop MPEG engine
	writel((readl(ve_regs + VE_CTRL) & ~0xf) | 0x7, ve_regs + VE_CTRL);

	return 1;
}
