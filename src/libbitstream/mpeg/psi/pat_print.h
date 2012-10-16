/*****************************************************************************
 * pat_print.h: ISO/IEC 13818-1 Program Allocation Table (PAT) (printing)
 *****************************************************************************
 * Copyright (C) 2009-2010 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef __BITSTREAM_MPEG_PAT_PRINT_H__
#define __BITSTREAM_MPEG_PAT_PRINT_H__

#include "../../common.h"
#include "psi.h"
#include "pat.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*****************************************************************************
 * Program Association Table
 *****************************************************************************/
static inline void pat_table_print(uint8_t **pp_sections, f_print pf_print,
                                   void *opaque, print_type_t i_print_type)
{
    uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
    uint8_t i;

    switch (i_print_type) {
    case PRINT_XML:
        pf_print(opaque, "<PAT tsid=\"%hu\" version=\"%hhu\" current_next=\"%d\">",
                 psi_table_get_tableidext(pp_sections),
                 psi_table_get_version(pp_sections),
                 !psi_table_get_current(pp_sections) ? 0 : 1);
        break;
    default:
        pf_print(opaque, "new PAT tsid=%hu version=%hhu%s",
                 psi_table_get_tableidext(pp_sections),
                 psi_table_get_version(pp_sections),
                 !psi_table_get_current(pp_sections) ? " (next)" : "");
    }

    for (i = 0; i <= i_last_section; i++) {
        uint8_t *p_section = psi_table_get_section(pp_sections, i);
        const uint8_t *p_program;
        int j = 0;

        while ((p_program = pat_get_program(p_section, j)) != NULL) {
            uint16_t i_program = patn_get_program(p_program);
            uint16_t i_pid = patn_get_pid(p_program);
            j++;
            switch (i_print_type) {
            case PRINT_XML:
                pf_print(opaque, "<PROGRAM number=\"%hu\" pid=\"%hu\"/>",
                         i_program, i_pid);
                break;
            default:
                if (i_program == 0)
                    pf_print(opaque, "  * NIT pid=%hu", i_pid);
                else
                    pf_print(opaque, "  * program number=%hu pid=%hu",
                             i_program, i_pid);
            }
        }
    }

    switch (i_print_type) {
    case PRINT_XML:
        pf_print(opaque, "</PAT>");
        break;
    default:
        pf_print(opaque, "end PAT");
    }
}

#ifdef __cplusplus
}
#endif

#endif
