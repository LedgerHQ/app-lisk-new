/*******************************************************************************
*   (c) 2018 - 2022 Zondax AG
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include <stdio.h>
#include <zxmacros.h>
#include <zxformat.h>
#include <zxtypes.h>

#include "coin.h"
#include "parser_common.h"
#include "parser_impl.h"
#include "parser.h"
#include "parser_print_items.h"

#include "txn_token_module.h"
#include "txn_auth_module.h"
#include "txn_pos_module.h"
#include "txn_legacy_module.h"
#include "txn_interop_module.h"

#include "crypto.h"
#include "crypto_helper.h"

#include "app_mode.h"

const char TAG_INIT[7] = "LSK_TX_";

#define PRINT_STANDARD_FIELDS(denom1, f1, f1_len, denom2, f2, f2_len) { \
    *pageCount = 1; \
    switch (displayIdx) { \
        case INTEROP_MAIN_REG_OWNCHAIN_ID_TYPE: \
            snprintf(outKey, outKeyLen, denom1); \
            pageStringHex(outVal, outValLen, (const char*) f1, f1_len, pageIdx, pageCount); \
            return parser_ok; \
        case INTEROP_MAIN_REG_OWNNAME_TYPE: \
            snprintf(outKey, outKeyLen, denom2); \
            pageStringExt(outVal, outValLen, f2, f2_len, pageIdx, pageCount); \
            return parser_ok; \
        default: \
            break;}}


parser_error_t parser_init_context(parser_context_t *ctx,
                                   const uint8_t *buffer,
                                   uint16_t bufferSize) {
    ctx->offset = 0;
    ctx->buffer = NULL;
    ctx->bufferLen = 0;

    if (bufferSize == 0 || buffer == NULL) {
        // Not available, use defaults
        return parser_init_context_empty;
    }

    ctx->buffer = buffer;
    ctx->bufferLen = bufferSize;
    return parser_ok;
}

parser_error_t parser_get_tag_chain(parser_context_t *ctx) {
    if (ctx->bufferLen < sizeof(TAG_INIT)) {
        return parser_unexpected_buffer_end;
    }
    if (MEMCMP(ctx->buffer, TAG_INIT, sizeof(TAG_INIT)) != 0) {
        return parser_unexpected_tag_init;
    }
    ctx->offset += sizeof(TAG_INIT);

    if(ctx->offset >= ctx->bufferLen) {
        return parser_unexpected_tag_init;
    }
    ctx->offset += CHAIN_ID_LENGTH;
    return parser_ok;
}

parser_error_t parser_parse(parser_context_t *ctx,
                            const uint8_t *data,
                            size_t dataLen,
                            parser_tx_t *tx_obj) {
    CHECK_ERROR(parser_init_context(ctx, data, dataLen))
    CHECK_ERROR(parser_get_tag_chain(ctx))

    ctx->tx_obj = tx_obj;
    return _read(ctx, tx_obj);
}

parser_error_t parser_validate(parser_context_t *ctx) {
    // Iterate through all items to check that all can be shown and are valid
    uint8_t numItems = 0;
    CHECK_ERROR(parser_getNumItems(ctx, &numItems))

    char tmpKey[40];
    char tmpVal[40];

    for (uint8_t idx = 0; idx < numItems; idx++) {
        uint8_t pageCount = 0;
        CHECK_ERROR(parser_getItem(ctx, idx, tmpKey, sizeof(tmpKey), tmpVal, sizeof(tmpVal), 0, &pageCount))
    }
    return parser_ok;
}

parser_error_t parser_getNumItems(const parser_context_t *ctx, uint8_t *num_items) {
    *num_items = _getNumItems(ctx);
    if(*num_items == 0) {
        return parser_unexpected_number_items;
    }
    return parser_ok;
}

static void cleanOutput(char *outKey, uint16_t outKeyLen,
                        char *outVal, uint16_t outValLen) {
    MEMZERO(outKey, outKeyLen);
    MEMZERO(outVal, outValLen);
    snprintf(outKey, outKeyLen, "?");
    snprintf(outVal, outValLen, " ");
}

static parser_error_t checkSanity(uint8_t numItems, uint8_t displayIdx) {
    if ( displayIdx >= numItems) {
        return parser_display_idx_out_of_range;
    }
    return parser_ok;
}

parser_error_t parser_getTxNumItems(__Z_UNUSED const parser_context_t *ctx, uint8_t *tx_num_items) {
    *tx_num_items = _getTxNumItems();
    return parser_ok;
}

// Get displayIdx item from context (ctx) and write the content in outKey and outVal
parser_error_t parser_getItem(const parser_context_t *ctx,
                              uint8_t displayIdx,
                              char *outKey, uint16_t outKeyLen,
                              char *outVal, uint16_t outValLen,
                              uint8_t pageIdx, uint8_t *pageCount) {

    *pageCount = 1;
    uint8_t numItems = 0;
    CHECK_ERROR(parser_getNumItems(ctx, &numItems))
    CHECK_APP_CANARY()

    CHECK_ERROR(checkSanity(numItems, displayIdx))
    cleanOutput(outKey, outKeyLen, outVal, outValLen);

    uint8_t txItems = 0;
    CHECK_ERROR(parser_getTxNumItems(ctx, &txItems))

    const uint8_t common_items = _getNumCommonItems();

    if (displayIdx < common_items) {
        return print_common_items(ctx, displayIdx, outKey, outKeyLen,
                                  outVal, outValLen, pageIdx, pageCount);
    }

    displayIdx-=common_items;
    uint8_t txDisplayIdx = 0;

    switch (ctx->tx_obj->module_id) {
        case TX_MODULE_ID_TOKEN:
            switch (ctx->tx_obj->command_id) {
                case TX_COMMAND_ID_TRANSFER:
                    return print_module_token_transfer(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);
                case TX_COMMAND_ID_CROSSCHAIN_TRANSFER:
                    return print_module_token_cross(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);
                default:
                    return parser_unexpected_value;
            }
        case TX_MODULE_ID_AUTH:
            CHECK_ERROR(getItem(displayIdx, &txDisplayIdx))
            return print_module_auth_reg(ctx, displayIdx, txDisplayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);

        case TX_MODULE_ID_POS:
            switch (ctx->tx_obj->command_id) {
                case TX_COMMAND_ID_REGISTER_VALIDATOR:
                    return print_module_pos_reg_validator(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);

                case TX_COMMAND_ID_STAKE:
                    CHECK_ERROR(getItem(displayIdx, &txDisplayIdx))
                    return print_module_pos_stake(ctx, displayIdx,  txDisplayIdx, outKey, outKeyLen,
                                       outVal, outValLen, pageIdx, pageCount);

                case TX_COMMAND_ID_CHANGE_COMMISSION:
                    return print_module_pos_change_commission(ctx, displayIdx, outKey, outKeyLen,
                                       outVal, outValLen, pageIdx, pageCount);
                case TX_COMMAND_ID_UNLOCK:
                case TX_COMMAND_ID_REPORT_MISBEHAVIOUR:
                case TX_COMMAND_ID_CLAIM_REWARDS:
                default:
                    return parser_unexpected_value;
            }

        case TX_MODULE_ID_LEGACY:
            switch (ctx->tx_obj->command_id) {
                case TX_COMMAND_ID_RECLAIM:
                    return print_module_legacy_reclaim(ctx, displayIdx, outKey, outKeyLen,
                                        outVal, outValLen, pageIdx, pageCount);
                case TX_COMMAND_ID_REGISTER_KEYS:
                    return print_module_legacy_register_keys(ctx, displayIdx, outKey, outKeyLen,
                        outVal, outValLen, pageIdx, pageCount);
                                default:
                    return parser_unexpected_value;
            }

        case TX_MODULE_ID_INTEROP:
            switch (ctx->tx_obj->command_id) {
                case TX_COMMAND_ID_MAINCHAIN_CC_UPDATE:
                    __attribute__((fallthrough));
                case TX_COMMAND_ID_SIDECHAIN_CC_UPDATE:
                    return print_module_interop_CCupdate(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);
                    break;
                case TX_COMMAND_ID_MAINCHAIN_REG:
                    PRINT_STANDARD_FIELDS("OwnChainID", ctx->tx_obj->tx_command._interop_mainchain_register.ownChainId, OWNCHAIN_ID_LENGTH,
                                          "OwnName", (const char*)ctx->tx_obj->tx_command._interop_mainchain_register.ownName,
                                          ctx->tx_obj->tx_command._interop_mainchain_register.ownNameLen);
                    break;
                case TX_COMMAND_ID_MSG_RECOVERY:
                    __attribute__((fallthrough));
                case TX_COMMAND_ID_MSG_RECOVERY_INIT:
                    __attribute__((fallthrough));
                case TX_COMMAND_ID_STATE_RECOVERY_INIT:
                    return print_module_interop_chainID(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);
                    break;
                case TX_COMMAND_ID_STATE_RECOVERY:
                    PRINT_STANDARD_FIELDS("ChainID", ctx->tx_obj->tx_command._interop_recover_state.chainID, OWNCHAIN_ID_LENGTH,
                                          "Module", (const char*) ctx->tx_obj->tx_command._interop_recover_state.module,
                                          ctx->tx_obj->tx_command._interop_recover_state.moduleLen);
                    break;
                case TX_COMMAND_ID_SIDECHAIN_REG:
                    return print_module_sidechain_register(ctx, displayIdx, outKey, outKeyLen,
                                      outVal, outValLen, pageIdx, pageCount);
                    break;
                default:
                    return parser_unexpected_value;
                    break;
            }
            break;

        default:
            return parser_unexpected_value;
            break;

    }

    return parser_display_idx_out_of_range;
}
