/*
 * Copyright (C) Jan 2013 Mellanox Technologies Ltd. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef REG_ACCESS_H
#define REG_ACCESS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <mtcr.h>
#include <tools_layouts/register_access_open_layouts.h>
#include <tools_layouts/register_access_sib_layouts.h>
#include <tools_layouts/cibfw_layouts.h>

// Hack, we include this for the MNV registers as they are not officialy a part of register_access so we defined them in tools.adb
#include <tools_layouts/tools_open_layouts.h>
#include <tools_layouts/reg_access_hca_layouts.h>

enum { // header lengths in bytes
    REG_ACCESS_MFBA_HEADER_LEN = 12,
};

typedef enum {
    REG_ACCESS_METHOD_GET = MACCESS_REG_METHOD_GET,
    REG_ACCESS_METHOD_SET = MACCESS_REG_METHOD_SET
} reg_access_method_t;

// we use the same error messages as mtcr
typedef MError reg_access_status_t;


const char* reg_access_err2str(reg_access_status_t status);
reg_access_status_t reg_access_mfba(mfile *mf, reg_access_method_t method, struct register_access_mfba *mfba);
reg_access_status_t reg_access_mfbe(mfile *mf, reg_access_method_t method, struct register_access_mfbe *mfbe);
reg_access_status_t reg_access_mfpa(mfile *mf, reg_access_method_t method, struct register_access_mfpa *mfpa);
reg_access_status_t reg_access_mfpa_new(mfile *mf, reg_access_method_t method, struct tools_open_mfpa *mfpa);
reg_access_status_t reg_access_mfmc(mfile *mf, reg_access_method_t method, struct tools_open_mfmc *mfmc);
reg_access_status_t reg_access_mnva(mfile *mf, reg_access_method_t method, struct tools_open_mnva *mnva);
reg_access_status_t reg_access_mnvi(mfile *mf, reg_access_method_t method, struct tools_open_mnvi *mnvi);
reg_access_status_t reg_access_mnvia(mfile *mf, reg_access_method_t method, struct tools_open_mnvia *mnvia);
reg_access_status_t reg_access_pcnr(mfile *mf, reg_access_method_t method, struct reg_access_hca_pcnr_reg *pcnr);
reg_access_status_t reg_access_mfrl(mfile *mf, reg_access_method_t method, struct cibfw_register_mfrl *mfrl);
reg_access_status_t reg_access_mfai(mfile *mf, reg_access_method_t method, struct cibfw_register_mfai *mfai);
reg_access_status_t reg_access_nvda(mfile *mf, reg_access_method_t method, struct tools_open_nvda *nvda);
reg_access_status_t reg_access_nvdi(mfile *mf, reg_access_method_t method, struct tools_open_nvdi *nvdi);
reg_access_status_t reg_access_nvia(mfile *mf, reg_access_method_t method, struct tools_open_nvia *nvdia);
reg_access_status_t reg_access_nvqc(mfile *mf, reg_access_method_t method, struct tools_open_nvqc *nvqc);
reg_access_status_t reg_access_nvqgc(mfile *mf, reg_access_method_t method, struct tools_open_nvqgc *nvqgc);
reg_access_status_t reg_access_mvts(mfile *mf, reg_access_method_t method, struct tools_open_mvts *mvts);
reg_access_status_t reg_access_mnvgn(mfile *mf, reg_access_method_t method, struct tools_open_mnvgn *mnvgn, int *status);
reg_access_status_t reg_access_mfmc(mfile *mf, reg_access_method_t method, struct tools_open_mfmc *mfmc);
reg_access_status_t reg_access_mpegc(mfile *mf, reg_access_method_t method, struct reg_access_hca_mpegc_reg *mpegc);

reg_access_status_t reg_access_mcam(mfile *mf, reg_access_method_t method, struct tools_open_mcam *mcam);
reg_access_status_t reg_access_secure_host(mfile *mf, reg_access_method_t method, struct tools_open_mlock *mlock);
/*
 * MCXX new burn commands
 */
reg_access_status_t reg_access_mcda(mfile *mf, reg_access_method_t method, struct reg_access_hca_mcda_reg *mcda);
reg_access_status_t reg_access_mqis(mfile *mf, reg_access_method_t method, struct reg_access_hca_mqis_reg *mqis);
reg_access_status_t reg_access_mcc(mfile *mf, reg_access_method_t method, struct reg_access_hca_mcc_reg *mcc );
reg_access_status_t reg_access_mcqs(mfile *mf, reg_access_method_t method, struct reg_access_hca_mcqs_reg *mcqs);
reg_access_status_t reg_access_mcqi(mfile *mf, reg_access_method_t method, struct reg_access_hca_mcqi_reg *mcqi);
reg_access_status_t reg_access_mgir(mfile *mf, reg_access_method_t method, struct tools_open_mgir *mgir);
reg_access_status_t reg_access_mtrc_cap(mfile *mf, reg_access_method_t method, struct reg_access_hca_mtrc_cap_reg *mtrc_cap);

const char* reg_access_err2str(reg_access_status_t status);

#ifdef __cplusplus
}
#endif

#endif // REG_ACCESS_H
