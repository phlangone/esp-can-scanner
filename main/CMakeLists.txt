idf_component_register(SRCS "main.c" 
                            "wifi_app.c" 
                            "http_server.c" 
                            "twai_app.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES webpage/styles.css 
                                webpage/script.js 
                                webpage/favicon.ico 
                                webpage/index.html)
