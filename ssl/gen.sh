rm server* 
rm dh1024.pem
rm openssl.cnf

set -e

#Generate a private key without password
#openssl genrsa -aes128 -passout pass:abcd -out server.key 2048
openssl genrsa -out server.key 2048

#Generate Certificate signing request

echo "
 [ req ]
 distinguished_name     = req_distinguished_name
 prompt = no

[ req_distinguished_name ]
commonName         = CoverFS
" > openssl.cnf

openssl req -new -config openssl.cnf -key server.key -out server.csr


#Sign certificate with private key
openssl x509 -req -days 3650 -in server.csr -signkey server.key -out server.crt


#Remove password requirement (needed for example)
#cp server.key server.key.secure
#openssl rsa -in server.key.secure -out server.key

#Generate dhparam file
openssl dhparam -out dh1024.pem 1024
