#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ProjetoSO2.h"
/*
    Vari?vel global para guardar o c?digo de erro que
    um processo filho eventualmente gere ao tentar executar.
*/
extern int errno;

/*
    Linha recomendada pelo man do linux 2.2.14
    Fonte: http://www.sjbaker.org/steve/software/signal_collection.html
*/
typedef void (*sighandler_t)(int);

/*
    Vetores de strings nos quais ser?o guardados as listas de
    argumentos na chamada de comando e de vari?veis de ambiente.
    Cada posi??o no vetor ? um argumento ou uma vari?vel de ambiente.
*/
static char *my_argv[100], *my_envp[100];
/*
    Vetor de strings que cont?m cada entrada da vari?vel de ambiente
    PATH separada. O objetivo ? procurar pelos comandos cujos caminhos
    completos n?o foram dados no PATH.
*/
static char *search_path[10];


struct job {
    int jid;           //numero do Job do usuario, começa com 0
    int pid;           //numero do Job do sitema
    char *nome;        //nome do processo
    int status;        //1 para parado, 0 pra rodando
};

/*
    Vetor que guarda os jobs sendo executados em background no momento.
    A quantidade m?xima de jobs em backgound simult?neos ? 100.
*/
struct job jobs[100];

int quantidade_jobs = 0;

/*
    guarda as informa??es do job em foreground, ?til para o tratamento
    dos sinal ctrl + z.
    Como jid n?o faz sentido neste contexto, ele ? usado para dizer se
    o dado ? v?lido (se h? algum processo executando em foreground).
    1 se houver um job executando, 0 caso contr?rio
*/
struct job job_executando;


/*
    Fun??o chamada caso ocorra um sinal (ctrl + C).
    O processo filho que est? executando o programa chamado
    pelo shell vai ser terminado, mas o pr?prio shell n?o pode
    terminar, por isso o sinal ? tratado. Em vez de encerrar,
    ele d? uma pr?xima linha de prompt.
*/
void handle_signal_int(int signo)
{
    printf("\nShell SOII> ");
    fflush(stdout);
    if(job_executando.jid){
        job_executando.jid = 0;
        kill(job_executando.pid, SIGKILL);
    }
}

/*
    Fun??o chamada caso ocorra um sinal (ctrl + Z).
*/
void handle_signal_stp(int signo)
{
    printf("\nShell SOII> ");
    fflush(stdout);
    //coloca o job que estava em foreground, se houver, para a lista de jobs em background
    //e faz o prompt retomar o que estava fazendo
    if(job_executando.jid){
        job_executando.jid = 0;
        if (quantidade_jobs == 0)
            jobs[quantidade_jobs].jid = 1;
        else
            jobs[quantidade_jobs].jid = jobs[quantidade_jobs - 1].jid + 1;
        jobs[quantidade_jobs].pid = job_executando.pid;
        jobs[quantidade_jobs].nome = (char *)malloc(sizeof(char) * 100);
        strcpy(jobs[quantidade_jobs].nome, job_executando.nome);
        jobs[quantidade_jobs].status = 1;
        ++quantidade_jobs;
        kill(job_executando.pid, SIGSTOP);
    }
}

/*
    Verifica cada processo filho. Os que j? acabaram s?o eliminados da lista,
    os que n?o foram t?m seus status atualizados.
*/
void atualiza_jobs()
{
    int i, j;

    for (i = 0; i < quantidade_jobs; ++i){
        if (waitpid(jobs[i].pid, NULL, WNOHANG) != 0){
            --quantidade_jobs;
            for(j = i; j < quantidade_jobs; ++j)
                jobs[j] = jobs[j+1];
        }
    }
}

/*
    Imprime a lista de jobs. As informa??es impressas s?o:
    [jid] [pid] Estado Nome
*/
void imprime_jobs(){
    int i;
    if (quantidade_jobs ==0)
    printf("Nenhum job na lista\n");
    for (i = 0; i < quantidade_jobs; ++i)
        if(jobs[i].status == 0)
            printf("[%d] [%d] Executando\t\t%s\n", jobs[i].jid, jobs[i].pid, jobs[i].nome);
        else
            printf("[%d] [%d] Parado\t\t%s\n", jobs[i].jid, jobs[i].pid, jobs[i].nome);
}


/*
    Percurre uma linha digitada pelo usu?rio quebrando
    ela em partes, separadas por uma quantidade indefinida
    de espa?os. Cada token ficar? guardado em uma posi??o da
    vari?vel global my_argv.

    C?digo baseado em: http://www.cplusplus.com/reference/clibrary/cstring/strtok/
*/
void fill_argv(char *tmp_argv)
{
    char *pch;
    int index = 0;

    pch = strtok (tmp_argv," ");
    while (pch != NULL)
    {
        my_argv[index] = (char *)malloc(sizeof(char) * strlen(pch) + 1);
        strncpy(my_argv[index], pch, strlen(pch));
        pch = strtok (NULL, " ");
        index++;
    }
}

/*
    Faz uma c?pia real das vari?veis de ambiente.
    N?o s? o ponteiro para a string ? copiada, mas a string em si.
    Isso ? feito para n?o trabalhar no nosso shell com o par?metro
    de entrada envp, do main(), mas uma c?pia dele, que fica na
    vari?vel global my_envp.
*/
void copy_envp(char **envp)
{
    int index = 0;
    for(;envp[index] != NULL; index++) {
        my_envp[index] = (char *)malloc(sizeof(char) * (strlen(envp[index]) + 1));
        memcpy(my_envp[index], envp[index], strlen(envp[index]));
    }
}

/*
    Procura pela vari?vel de ambiente PATH, no vetor de strings
    de vari?veis de ambiente passado.
    Caso n?o exista essa vari?vel, o shell d? pau! Segmentation fault na hora! ;D
*/
void get_path_string(char **tmp_envp, char *bin_path)
{
    int count = 0;
    char *tmp;
    while(1) {
        /*
            linhas seguintes foram alteradas para resolver um problema.
            Na instala??o do linux que testamos, havia uma vari?vel de
            ambiente chamada DEFAULTS_PATH, com a qual essa fun??o casava.
            S? que o objetivo n?o era pegar ela.
            Para resolver, acrescentamos um = na string de busca, para assegurar
            que nome da vari?vel de ambiente achada termina com PATH.
            No if, acrescentamos a condi??o (tmp != tmp_envp[count]) para
            assegurar que casou com o come?o da string, logo, n?o h?
            nada antes de PATH, no nome da vari?vel. Resolvido o problema.
        */
        tmp = strstr(tmp_envp[count], "PATH=");
        if((tmp == NULL) || (tmp != tmp_envp[count])) {
            count++;
        } else {
            break;
        }
    }
        strncpy(bin_path, tmp, strlen(tmp));
}

/*
    Quebra a string da vari?vel de ambiente PATH e popula
    um vetor global chamado search_path, no qual cada posi??o
    ? uma das pastas que est? no caminho.
*/
void insert_path_str_to_search(char *path_str)
{
    int index=0;
    char *tmp = path_str;
    char ret[100];

    while(*tmp != '=')
        tmp++;
    tmp++;

    while(*tmp != '\0') {
        if(*tmp == ':') {
            strncat(ret, "/", 1);
            search_path[index] = (char *) malloc(sizeof(char) * (strlen(ret) + 1));
            strncat(search_path[index], ret, strlen(ret));
            strncat(search_path[index], "\0", 1);
            index++;
            bzero(ret, 100);
        } else {
            strncat(ret, tmp, 1);
        }
        tmp++;
    }
}

/*
    Procura pelo comando no search_path, que ?
    o vetor que cont?m as pastas designadas em PATH.
    Se o encontrar, o comando passado como par?metro
    recebe como prefixo a pasta onde ele est?, ficando
    pronto para ser usado em execve. Caso contr?rio, ele
    n?o ? alterado.
*/
int attach_path(char *cmd)
{
    char ret[100];
    int index;
    int fd;
    bzero(ret, 100);
    for(index=0;search_path[index]!=NULL;index++) {
        strcpy(ret, search_path[index]);
        strncat(ret, cmd, strlen(cmd));
        if((fd = open(ret, O_RDONLY)) > 0) {
            strncpy(cmd, ret, strlen(ret));
            close(fd);
            return 0;
        }
    }
    return 0;
}

/*
    Fun??o feita para chamar execve, cria um processo filho
    e faz ele chamar o comando. Caso o comando n?o exista,
    uma mensagem de erro ? mostrada. Caso o comando exista,
    e background for 0 o processo pai (o shell) fica esperando
    o fim da execu??o do filho, para dar a pr?xima linha do prompt.
    Caso background seja diferente de 0, o processo pai n?o espera pelo t?rmino do filho,
    e o processo filho ? colocado no vetor global jobs[].
*/
void call_execve(char *cmd, int background)
{
    int i;
    int pid;

    pid = fork();
    //printf("o comando eh %s\n", cmd);
    if(pid == 0) {
        /*
            Estes sinais s?o os padr?o para ctrl + C e ctrl + Z no shell
            ent?o tive que desabilitar no processo filho, para que um sinal
            mandado n?o mate todos os processos em background.
            Mas, fazendo isso, n?o podia eu mesmo mandar sinal para um processo
            filho em foreground, que deveria receb?-lo. Para resolver, acabei usando
            outros sinais que, neste contexto, t?m o mesmo efeito.
            No caso, foi usado o SIGKILL no lugar do SIGINT e SIGSTOP no lugar do SIGTSTP.
            Perceba que nas fun??es handler de sinais, o sinal mandado ao processo filho
            em foreground (caso exista) s?o esses substitutos.
        */
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        i = execve(cmd, my_argv, my_envp);
        //printf("errno eh %d\n", errno);
        if(i < 0) {
            printf("%s: %s\n", cmd, "comando nao encontrado");
            exit(1);
        }
    } else {
        //se o processo filho deve rodar em background,
        //coloco no vetor de jobs.
        // caso contr?rio, espero o processo filho acabar
        if (background){
            if (quantidade_jobs == 0)
                jobs[quantidade_jobs].jid = 1;
            else
                jobs[quantidade_jobs].jid = jobs[quantidade_jobs - 1].jid + 1;
            jobs[quantidade_jobs].pid = pid;
            jobs[quantidade_jobs].nome = (char *)malloc(sizeof(char) * 100);
            strcpy(jobs[quantidade_jobs].nome, cmd);
            jobs[quantidade_jobs].status = 0;
            ++quantidade_jobs;
        }
        else {
            //coloca job na vari?vel global job_executando, que pode ser ?til
            //caso o sinal ctrl + z seja recebido
            job_executando.jid = 1;
            job_executando.pid = pid;
            job_executando.nome = (char *)malloc(sizeof(char) * 100);
            strcpy(job_executando.nome, cmd);
            job_executando.status = 0;
            //faz o shell ficar esperando pelo t?rmino do filho
            //tive que fazer uma espera ocupada por causa do tratamento de sinais
            //solu??o p?ssima, eu sei. Mas tentei de tudo quanto foi jeito e s? isso
            //resolvia.
            /*
                Explica??o longa de porque s? isso resolve:
                n?o h? como "destravar" o programa que est? aguardando o fim de um
                processor com waitpid(). Ent?o o tratamento de sinais ctrl + Z n?o conseguia
                destravar o shell.
                Tentei fazer com que a leitura de comando recome?asse explicitamente, dentro
                da fun??o que trata o ctrl + Z, em vez de destravar o waitpid pendente. Mas
                da? o segundo ctrl + Z dado no prompt n?o funcionaria, porque j? havia um sinal
                sendo tratado.
            */
            while ((waitpid(pid, NULL, WNOHANG) == 0) && (job_executando.jid == 1));
            job_executando.jid = 0;
        }
    }
}

/*
    Zera o vetor global de string que cont?m cada
    argumento da linha de comando separado.
*/
void free_argv()
{
    int index;
    for(index=0;my_argv[index]!=NULL;index++) {
        bzero(my_argv[index], strlen(my_argv[index])+1);
        my_argv[index] = NULL;
        free(my_argv[index]);
    }
}









// Função principal, que organiza as chamadas, e faz as chamadas das funções implementadas
int main(int argc, char *argv[], char *envp[])
{
        int i;
    char *path_str = (char *)malloc(sizeof(char) * 256);
    char c;
    int fd;
    char *tmp = (char *)malloc(sizeof(char) * 100);
    char *cmd = (char *)malloc(sizeof(char) * 100);
    int background;

        job_executando.jid = 0;

/*
    Acerta as vari?veis de ambiente no vetor my_envp e j? trata da
    string de PATH. Os vetores globais my_envp e search_path j? est?o
    iniciados.
*/
    copy_envp(envp);
    get_path_string(my_envp, path_str);
    insert_path_str_to_search(path_str);


    //Limpa a tela do Shell principal, dando a impressao de abrir uma Shell nova
    if(fork() == 0) {
        execve("/usr/bin/clear", argv, my_envp);
        exit(1);
    } else {
        wait(NULL);
    }
    printf("Shell SOII> ");
    fflush(stdout);


/*
    Associa o recebimento dos sinais ctrl + C ou ctrl + Z com as fun??es que os tratam
*/
    //a princ?pio, o programa deve ignorar o ctrl + C e ctrl + Z, por isso essa
    //pr?xima linha
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    //depois, associamos a fun??o feita para tratar o ctrl + C e ctrl + Z
    signal(SIGINT, handle_signal_int);
    signal(SIGTSTP, handle_signal_stp);
/*
    Vai lendo caractere por caractere que o usu?rio digita, at?
    que ele mande um ctrl + D, que encerra o shell
*/
    while(c != EOF) {
        c = getchar();
        switch(c) {
            case '\n': if(tmp[0] == '\0') {
                       printf("Shell SOII> ");
                   } else {
                                   background = 0;

                    //procura o & na string
                    if (index(tmp, '&') != NULL){
                                    background = 1;
                                    *(index(tmp, '&')) = '\0';
                       }
                    fill_argv(tmp);
                    strncpy(cmd, my_argv[0], strlen(my_argv[0]));
                    strncat(cmd, "\0", 1);

                    //Comando =========== PWD ================
                    if (strcmp(cmd, "pwd") == 0){
                        printf("%s\n", getcwd (NULL, 0));
                    }
                    //Comando =========== CD ================
                    else if (strcmp(cmd, "cd") == 0){
                                   if (chdir(my_argv[1]) != 0)
                                               printf("Arquivo ou diretorio nao encontrado: %s\n", my_argv[1]);
                           }
                        //Comando =========== jobs ================
                           else if (strcmp(cmd, "jobs") == 0){
                                   atualiza_jobs();
                                   imprime_jobs();
                        }
                        //Comando =========== quit ================
                        else if ((strcmp(cmd, "quit") == 0) || (strcmp(cmd, "exit") == 0)){
                                   int proc_parado = 0;
                            atualiza_jobs();
                                           for (i = 0; i < quantidade_jobs; ++i)
                                                if(jobs[i].status == 1){
                                                    proc_parado = 1;
                                                    break;
                                                }
                                            if(proc_parado)
                                                   printf("Existem trabalhos parados\n");
                                            else
                                                exit(0);
                           }
                          //fg, traz um processo em background para foreground
                           //Comando =========== FG ================
                        else if (strcmp(cmd, "fg") == 0){
                                            int pid = 0;
                                            int achou;
                            if (my_argv[1])
                                                   pid = atoi(my_argv[1]);
                                            if ((pid == 0) && (quantidade_jobs > 0))
                                                pid = jobs[0].pid;
                                            achou = 0;
                                            for(i = 0; i < quantidade_jobs; ++i){
                                                if((jobs[i].pid == pid)){
                                                        pid = i;
                                                        achou = 1;
                                                        break;
                                                }
                                else if((jobs[i].jid == pid)){
                                                        pid = i;
                                                       achou = 1;
                                                        break;
                                                }
                                            }
                                            //se achou, certeza que pid agora ? o ?ndice do vetor no qual est?
                                            //o processo a ser trazido para foreground
                                            if (achou){
                                                job_executando.jid = 1;
                                                job_executando.pid = jobs[pid].pid;
                                                job_executando.nome = (char *)malloc(sizeof(char) * 100);
                                                strcpy(job_executando.nome, jobs[pid].nome);
                                                job_executando.status = 0;
                                                quantidade_jobs--;
                                                for(i = pid; i < quantidade_jobs; ++i)
                                                        jobs[i] = jobs[i+1];
                                                printf("%s", job_executando.nome);
                                                fflush(stdout);
                                                kill(job_executando.pid, SIGCONT);
                                                while ((waitpid(job_executando.pid, NULL, WNOHANG) == 0) && (job_executando.jid == 1));
                                                    job_executando.jid = 0;
                                            }
                                            else
                                                printf("Trabalho nao existe\n");

                           }


                        //bg, manda o processo para background
                        //Comando =========== BG ================
                        else if (strcmp(cmd, "bg") == 0){
                                            int pid = 0;
                                            int achou;

                                            if (my_argv[1])
                                                pid = atoi(my_argv[1]);
                                            if ((pid == 0) && (quantidade_jobs > 0))
                                                for(i = 0; i < quantidade_jobs; ++i)
                                                //essa segunda condi??o no if ? s? para emular a mensagem de erro
                                                //dada pelo prompt. Assim, ele imprimir? que o trabalho j? est? em
                                                //segundo plano, porque certamente pid estar? com uma id v?lida (que
                                                //est? no vetor de jobs)
                                                    if((jobs[i].status == 1) || (i == quantidade_jobs - 1)){
                                                           pid = jobs[i].pid;
                                                           break;
                                                    }
                                            achou = 0;
                                            for(i = 0; i < quantidade_jobs; ++i){
                                                if((jobs[i].pid == pid)){
                                                        pid = i;
                                                        achou = 1;
                                                        break;
                                                }
                                                else if((jobs[i].jid == pid)){
                                                        pid = i;
                                                        achou = 1;
                                                        break;
                                                }
                                            }

                            //se achou, certeza que pid agora ? o ?ndice do vetor no qual est?
                                            //o processo a ser trazido para foreground
                                            if (achou){
                                                if (jobs[pid].status == 0)
                                                       printf("O trabalho ja esta em segundo plano\n");
                                                else {
                                                        kill(jobs[pid].pid, SIGCONT);
                                                        jobs[pid].status = 0;
                                                }
                                            }
                                            else
                                                printf("Trabalho nao existe\n");

                           }


                        //se n?o for nenhum comando especial
                           else if(index(cmd, '/') == NULL) {
                           /*
                                        A attach_path sempre retorna 0, mas ela poderia
                                        retornar um c?digo de erro. Esse if prev? a implementa??o
                                        mais sofisticada de attach_path
                           */
                            if(attach_path(cmd) == 0)
                               call_execve(cmd, background);
                                else {
                               printf("%s: comando nao encontrado\n", cmd);
                               }
                           } else {
                           if((fd = open(cmd, O_RDONLY)) > 0) {
                               close(fd);
                               call_execve(cmd, background);
                           } else {
                               printf("%s: comando nao encontrado\n", cmd);
                           }
                       }
                       free_argv();
                       printf("Shell SOII> ");
                       bzero(cmd, 100);
                   }
                   bzero(tmp, 100);
                   break;
            default: strncat(tmp, &c, 1);
                 break;
        }
    }

    free(tmp);

    free(path_str);
    for(i=0;my_envp[i]!=NULL;i++)
        free(my_envp[i]);
    for(i=0;i<10;i++)
        free(search_path[i]);
    for(i=0;jobs[i].nome!=NULL;i++)
        free(jobs[i].nome);
    printf("\n");
    return 0;
}
