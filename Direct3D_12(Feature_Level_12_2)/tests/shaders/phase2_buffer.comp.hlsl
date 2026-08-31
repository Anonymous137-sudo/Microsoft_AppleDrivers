ByteAddressBuffer input_buffer : register(t0);
RWByteAddressBuffer output_buffer : register(u0);
RWByteAddressBuffer output_buffer_b : register(u1);

cbuffer ComputeParameters : register(b0)
{
    uint addend;
    uint3 reserved;
};

[numthreads(4, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint value = input_buffer.Load(dispatch_thread_id.x * 4u);
    output_buffer.Store(dispatch_thread_id.x * 4u, value + addend);
    output_buffer_b.Store(dispatch_thread_id.x * 4u, value * 2u + addend);
}
