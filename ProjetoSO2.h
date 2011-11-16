
#ifndef ProjetoSO2
#define ProjetoSO2

//cabe?alhos de todas as fun??es do programa
void handle_signal_int(int signo);
void handle_signal_stp(int signo);
void atualiza_jobs();
void imprime_jobs();
void fill_argv(char *tmp_argv);
void copy_envp(char **envp);
void get_path_string(char **tmp_envp, char *bin_path);
void insert_path_str_to_search(char *path_str);
int attach_path(char *cmd);
void call_execve(char *cmd, int background);
void free_argv();

#endif
