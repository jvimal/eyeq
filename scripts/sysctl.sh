
sysctl -w net.core.rmem_max=16777216 
sysctl -w net.core.wmem_max=16777216

sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"
sysctl -w net.ipv4.tcp_wmem="4096 65536 16777216"

