all: btchat
btchat: btchat.c
	gcc ./btchat.c -O3 -o ./btchat -lbluetooth
debug: btchat.c
	gcc ./btchat.c -g -O3 -o ./btchat_dbg -lbluetooth
clean:
	rm -f ./btchat ./btchat_dbg
