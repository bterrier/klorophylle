// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>

// Chat Completions SSE conformance fixtures (ADR 0019 decision 3: each
// provider's quirks pinned as replayable streams).
//
// PROVENANCE: the Chat Completions fixtures were hand-authored 2026-06-11, the
// native-dialect ones (Anthropic / OpenAI Responses / Gemini) 2026-06-14,
// from each provider's documented wire format — no live endpoint was used.
// Replace any of these with a real
// capture when an endpoint is at hand (curl -N --no-buffer, outside ctest) and
// update this note with the capture date.
//
// Payloads use NORMAL string literals with ' standing for " and ` standing
// for \" , converted by fixtures::wire() — this Qt's moc mis-lexes raw
// string literals containing \" or unbalanced braces (it silently drops the
// including test class), and fully escaped literals are unreadable. Don't
// put legitimate apostrophes or backticks in payload text.

namespace fixtures {

inline QByteArray wire(const char *singleQuoted)
{
    QByteArray bytes(singleQuoted);
    bytes.replace('`', "\\\"");
    bytes.replace('\'', '"');
    return bytes;
}

// api.openai.com: role-only first chunk, per-token content deltas, a
// finish_reason chunk, then (stream_options.include_usage) a usage-only
// chunk with empty choices, then the [DONE] sentinel.
inline QByteArray openaiTextStream()
{
    return wire(
        "data: {'id':'chatcmpl-1','object':'chat.completion.chunk','model':'gpt-4o-mini','system_fingerprint':'fp_1','choices':[{'index':0,'delta':{'role':'assistant','content':''},'logprobs':null,'finish_reason':null}]}\n\n"
        "data: {'id':'chatcmpl-1','object':'chat.completion.chunk','choices':[{'index':0,'delta':{'content':'Your basil'},'finish_reason':null}]}\n\n"
        "data: {'id':'chatcmpl-1','object':'chat.completion.chunk','choices':[{'index':0,'delta':{'content':' needs water.'},'finish_reason':null}]}\n\n"
        "data: {'id':'chatcmpl-1','object':'chat.completion.chunk','choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n"
        "data: {'id':'chatcmpl-1','object':'chat.completion.chunk','choices':[],'usage':{'prompt_tokens':12,'completion_tokens':34,'total_tokens':46}}\n\n"
        "data: [DONE]\n\n");
}

// api.openai.com parallel tool calls: id/name in the first chunk per index,
// argument fragments split mid-JSON-token, indices interleaved.
inline QByteArray openaiParallelToolCallsStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'role':'assistant','content':null,'tool_calls':[{'index':0,'id':'call_a','type':'function','function':{'name':'list_plants','arguments':''}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'tool_calls':[{'index':1,'id':'call_b','type':'function','function':{'name':'read_plant_data','arguments':''}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'tool_calls':[{'index':0,'function':{'arguments':'{}'}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'tool_calls':[{'index':1,'function':{'arguments':'{`plantId`'}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'tool_calls':[{'index':1,'function':{'arguments':': `p-1`}'}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'tool_calls'}]}\n\n"
        "data: {'choices':[],'usage':{'prompt_tokens':80,'completion_tokens':20,'total_tokens':100}}\n\n"
        "data: [DONE]\n\n");
}

// Ollama /v1/chat/completions: the whole tool call arrives in ONE chunk
// (complete arguments string), usage rides the finish chunk.
inline QByteArray ollamaToolCallStream()
{
    return wire(
        "data: {'id':'chatcmpl-ol1','object':'chat.completion.chunk','model':'qwen3','choices':[{'index':0,'delta':{'role':'assistant','content':'','tool_calls':[{'index':0,'id':'call_x9','type':'function','function':{'name':'read_plant_data','arguments':'{`plantId`:`p-1`}'}}]},'finish_reason':null}]}\n\n"
        "data: {'id':'chatcmpl-ol1','object':'chat.completion.chunk','model':'qwen3','choices':[{'index':0,'delta':{},'finish_reason':'tool_calls'}],'usage':{'prompt_tokens':210,'completion_tokens':18,'total_tokens':228}}\n\n"
        "data: [DONE]\n\n");
}

// llama.cpp server: content deltas plus its non-standard 'timings' object
// on the final chunks — must be ignored, not an error.
inline QByteArray llamaServerTextStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'content':'Soil '},'finish_reason':null}],'created':1760000000,'model':'qwen3-4b'}\n\n"
        "data: {'choices':[{'index':0,'delta':{'content':'is dry.'},'finish_reason':null}],'created':1760000000,'model':'qwen3-4b'}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}],'created':1760000000,'model':'qwen3-4b','timings':{'prompt_n':12,'predicted_n':8,'predicted_per_second':41.5}}\n\n"
        "data: {'choices':[],'usage':{'prompt_tokens':12,'completion_tokens':8},'timings':{'prompt_n':12,'predicted_n':8}}\n\n"
        "data: [DONE]\n\n");
}

// OpenRouter: leading comment keepalives while the upstream model spins up.
inline QByteArray openrouterKeepaliveStream()
{
    return wire(
        ": OPENROUTER PROCESSING\n\n"
        ": OPENROUTER PROCESSING\n\n"
        "data: {'choices':[{'index':0,'delta':{'role':'assistant','content':'Hi'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n"
        "data: [DONE]\n\n");
}

// DeepSeek-style structured reasoning: reasoning_content deltas first, then
// regular content deltas.
inline QByteArray deepseekReasoningStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'role':'assistant','reasoning_content':'moisture 12% is '},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'reasoning_content':'below the 20% floor'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'content':'Water the basil.'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n"
        "data: [DONE]\n\n");
}

// Qwen3-style inline reasoning over plain Chat Completions: <think> tags in
// the content stream, the close tag split across two deltas.
inline QByteArray qwenThinkTagStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'role':'assistant','content':'<think>dry soil, '},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'content':'low light</thi'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{'content':'nk>Move it and water.'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n"
        "data: [DONE]\n\n");
}

// A well-formed stream that uses CRLF line terminators throughout.
inline QByteArray crlfStream()
{
    QByteArray stream = wire(
        "data: {'choices':[{'index':0,'delta':{'content':'Hi'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n"
        "data: [DONE]\n\n");
    stream.replace("\n", "\r\n");
    return stream;
}

// finish_reason seen but the proxy never sends [DONE] — still a success.
inline QByteArray noSentinelStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'content':'Hi'},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]}\n\n");
}

// Clean HTTP end with neither [DONE] nor any finish_reason — a protocol
// violation the provider must surface as Parse.
inline QByteArray truncatedStream()
{
    return wire("data: {'choices':[{'index':0,'delta':{'content':'Hi'},'finish_reason':null}]}\n\n");
}

// A tool call whose accumulated arguments never parse as JSON.
inline QByteArray malformedArgsStream()
{
    return wire(
        "data: {'choices':[{'index':0,'delta':{'tool_calls':[{'index':0,'id':'call_z','type':'function','function':{'name':'read_plant_data','arguments':'{`plantId`: '}}]},'finish_reason':null}]}\n\n"
        "data: {'choices':[{'index':0,'delta':{},'finish_reason':'tool_calls'}]}\n\n"
        "data: [DONE]\n\n");
}

// HTTP error bodies: OpenAI object shape, Ollama string shape.
inline QByteArray openaiErrorBody401()
{
    return wire(
        "{'error':{'message':'Incorrect API key provided.','type':'invalid_request_error','code':'invalid_api_key'}}");
}

inline QByteArray ollamaErrorBody()
{
    return wire("{'error':'model not found, try pulling it first'}");
}

// ---------------------------------------------------------------------------
// Anthropic Messages — typed SSE events (event: + data:), no [DONE];
// usage split across message_start (input) and message_delta (output); the
// stream ends with message_stop.
// ---------------------------------------------------------------------------

// Text turn: message_start carries input tokens, two text deltas, then
// message_delta (end_turn + output tokens) and message_stop.
inline QByteArray anthropicTextStream()
{
    return wire(
        "event: message_start\n"
        "data: {'type':'message_start','message':{'id':'msg_1','role':'assistant','model':'claude','usage':{'input_tokens':14,'output_tokens':1}}}\n\n"
        "event: content_block_start\n"
        "data: {'type':'content_block_start','index':0,'content_block':{'type':'text','text':''}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':'Your basil'}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':' needs water.'}}\n\n"
        "event: content_block_stop\n"
        "data: {'type':'content_block_stop','index':0}\n\n"
        "event: message_delta\n"
        "data: {'type':'message_delta','delta':{'stop_reason':'end_turn','stop_sequence':null},'usage':{'output_tokens':27}}\n\n"
        "event: message_stop\n"
        "data: {'type':'message_stop'}\n\n");
}

// Tool use: a tool_use content block with id+name, then input_json_delta args
// split mid-token, then message_delta(stop_reason tool_use) + message_stop.
inline QByteArray anthropicToolUseStream()
{
    return wire(
        "event: message_start\n"
        "data: {'type':'message_start','message':{'id':'msg_2','role':'assistant','usage':{'input_tokens':40,'output_tokens':1}}}\n\n"
        "event: content_block_start\n"
        "data: {'type':'content_block_start','index':0,'content_block':{'type':'tool_use','id':'toolu_7','name':'read_plant_data','input':{}}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'input_json_delta','partial_json':'{`plantId`'}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'input_json_delta','partial_json':': `p-1`}'}}\n\n"
        "event: content_block_stop\n"
        "data: {'type':'content_block_stop','index':0}\n\n"
        "event: message_delta\n"
        "data: {'type':'message_delta','delta':{'stop_reason':'tool_use'},'usage':{'output_tokens':19}}\n\n"
        "event: message_stop\n"
        "data: {'type':'message_stop'}\n\n");
}

// Extended thinking: a thinking block with two thinking deltas and a trailing
// signature_delta (the echoed opaque), then a text block.
inline QByteArray anthropicThinkingStream()
{
    return wire(
        "event: message_start\n"
        "data: {'type':'message_start','message':{'id':'msg_3','role':'assistant','usage':{'input_tokens':30,'output_tokens':1}}}\n\n"
        "event: content_block_start\n"
        "data: {'type':'content_block_start','index':0,'content_block':{'type':'thinking','thinking':''}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'thinking_delta','thinking':'moisture is '}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'thinking_delta','thinking':'below the floor'}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'signature_delta','signature':'sig-abc123'}}\n\n"
        "event: content_block_stop\n"
        "data: {'type':'content_block_stop','index':0}\n\n"
        "event: content_block_start\n"
        "data: {'type':'content_block_start','index':1,'content_block':{'type':'text','text':''}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':1,'delta':{'type':'text_delta','text':'Water the basil.'}}\n\n"
        "event: content_block_stop\n"
        "data: {'type':'content_block_stop','index':1}\n\n"
        "event: message_delta\n"
        "data: {'type':'message_delta','delta':{'stop_reason':'end_turn'},'usage':{'output_tokens':40}}\n\n"
        "event: message_stop\n"
        "data: {'type':'message_stop'}\n\n");
}

// A partial stream that never reaches message_stop — for drop/cancel/timeout
// (the complete streams finish on message_stop before the test can act).
inline QByteArray anthropicPartialStream()
{
    return wire(
        "event: message_start\n"
        "data: {'type':'message_start','message':{'id':'msg_p','role':'assistant','usage':{'input_tokens':3,'output_tokens':1}}}\n\n"
        "event: content_block_start\n"
        "data: {'type':'content_block_start','index':0,'content_block':{'type':'text','text':''}}\n\n"
        "event: content_block_delta\n"
        "data: {'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':'Hi'}}\n\n");
}

// A mid-stream error event (e.g. overloaded) — surfaces as a Provider error.
inline QByteArray anthropicErrorEventStream()
{
    return wire(
        "event: message_start\n"
        "data: {'type':'message_start','message':{'id':'msg_4','role':'assistant','usage':{'input_tokens':5,'output_tokens':1}}}\n\n"
        "event: error\n"
        "data: {'type':'error','error':{'type':'overloaded_error','message':'Overloaded'}}\n\n");
}

// HTTP 401 body (authentication_error) in Anthropic's object shape.
inline QByteArray anthropicErrorBody401()
{
    return wire("{'type':'error','error':{'type':'authentication_error','message':'invalid x-api-key'}}");
}

// ---------------------------------------------------------------------------
// OpenAI Responses — typed SSE events; the stream closes after
// response.completed (no [DONE]); usage + stop reason ride that event.
// ---------------------------------------------------------------------------

inline QByteArray responsesTextStream()
{
    return wire(
        "event: response.created\n"
        "data: {'type':'response.created','response':{'id':'resp_1','status':'in_progress'}}\n\n"
        "event: response.output_text.delta\n"
        "data: {'type':'response.output_text.delta','output_index':0,'delta':'Your basil'}\n\n"
        "event: response.output_text.delta\n"
        "data: {'type':'response.output_text.delta','output_index':0,'delta':' needs water.'}\n\n"
        "event: response.completed\n"
        "data: {'type':'response.completed','response':{'id':'resp_1','status':'completed','output':[{'type':'message','role':'assistant'}],'usage':{'input_tokens':11,'output_tokens':22,'total_tokens':33}}}\n\n");
}

inline QByteArray responsesToolCallStream()
{
    return wire(
        "event: response.output_item.added\n"
        "data: {'type':'response.output_item.added','output_index':0,'item':{'type':'function_call','id':'fc_1','call_id':'call_x','name':'read_plant_data','arguments':''}}\n\n"
        "event: response.function_call_arguments.delta\n"
        "data: {'type':'response.function_call_arguments.delta','output_index':0,'delta':'{`plantId`'}\n\n"
        "event: response.function_call_arguments.delta\n"
        "data: {'type':'response.function_call_arguments.delta','output_index':0,'delta':': `p-1`}'}\n\n"
        "event: response.completed\n"
        "data: {'type':'response.completed','response':{'status':'completed','output':[{'type':'function_call','call_id':'call_x','name':'read_plant_data'}],'usage':{'input_tokens':50,'output_tokens':12}}}\n\n");
}

// Reasoning summary deltas, then output_item.done carrying the encrypted
// reasoning item (the opaque echo), then the visible answer.
inline QByteArray responsesReasoningStream()
{
    return wire(
        "event: response.reasoning_summary_text.delta\n"
        "data: {'type':'response.reasoning_summary_text.delta','output_index':0,'delta':'checking '}\n\n"
        "event: response.reasoning_summary_text.delta\n"
        "data: {'type':'response.reasoning_summary_text.delta','output_index':0,'delta':'moisture'}\n\n"
        "event: response.output_item.done\n"
        "data: {'type':'response.output_item.done','output_index':0,'item':{'type':'reasoning','id':'rs_1','encrypted_content':'enc-blob','summary':[]}}\n\n"
        "event: response.output_text.delta\n"
        "data: {'type':'response.output_text.delta','output_index':1,'delta':'Water it.'}\n\n"
        "event: response.completed\n"
        "data: {'type':'response.completed','response':{'status':'completed','output':[{'type':'reasoning','id':'rs_1'},{'type':'message'}],'usage':{'input_tokens':20,'output_tokens':30}}}\n\n");
}

// A partial stream that never reaches response.completed — drop/cancel/timeout.
inline QByteArray responsesPartialStream()
{
    return wire(
        "event: response.created\n"
        "data: {'type':'response.created','response':{'id':'resp_p','status':'in_progress'}}\n\n"
        "event: response.output_text.delta\n"
        "data: {'type':'response.output_text.delta','output_index':0,'delta':'Hi'}\n\n");
}

// A response.failed event -> Provider error.
inline QByteArray responsesFailedStream()
{
    return wire(
        "event: response.failed\n"
        "data: {'type':'response.failed','response':{'status':'failed','error':{'code':'server_error','message':'boom'}}}\n\n");
}

// ---------------------------------------------------------------------------
// Gemini generateContent — bare SSE data chunks (no event: types, no
// [DONE]); finishReason + usageMetadata on the last chunk.
// ---------------------------------------------------------------------------

inline QByteArray geminiTextStream()
{
    return wire(
        "data: {'candidates':[{'content':{'role':'model','parts':[{'text':'Your basil'}]}}]}\n\n"
        "data: {'candidates':[{'content':{'role':'model','parts':[{'text':' needs water.'}]},'finishReason':'STOP'}],'usageMetadata':{'promptTokenCount':9,'candidatesTokenCount':18,'totalTokenCount':27}}\n\n");
}

// Whole function call in one part (complete args), finishReason STOP — Gemini
// signals tool use by the presence of the functionCall, not the reason.
inline QByteArray geminiToolCallStream()
{
    return wire(
        "data: {'candidates':[{'content':{'role':'model','parts':[{'functionCall':{'name':'read_plant_data','args':{'plantId':'p-1'}}}]},'finishReason':'STOP'}],'usageMetadata':{'promptTokenCount':40,'candidatesTokenCount':10}}\n\n");
}

// A thought part (thought:true with a thoughtSignature) then the visible text.
inline QByteArray geminiThoughtStream()
{
    return wire(
        "data: {'candidates':[{'content':{'role':'model','parts':[{'text':'considering moisture','thought':true,'thoughtSignature':'tsig-1'}]}}]}\n\n"
        "data: {'candidates':[{'content':{'role':'model','parts':[{'text':'Water it.'}]},'finishReason':'STOP'}],'usageMetadata':{'promptTokenCount':15,'candidatesTokenCount':20}}\n\n");
}

// A partial stream that never sends finishReason — drop/cancel/timeout.
inline QByteArray geminiPartialStream()
{
    return wire("data: {'candidates':[{'content':{'role':'model','parts':[{'text':'Hi'}]}}]}\n\n");
}

// An error object in the stream -> Provider error.
inline QByteArray geminiErrorChunkStream()
{
    return wire("data: {'error':{'code':503,'message':'overloaded','status':'UNAVAILABLE'}}\n\n");
}

// HTTP 401 body in Gemini's error-object shape.
inline QByteArray geminiErrorBody401()
{
    return wire("{'error':{'code':401,'message':'API key not valid','status':'INVALID_ARGUMENT'}}");
}

} // namespace fixtures
