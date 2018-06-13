#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_LENGTH 1000
#define SLEEP_TIME 1

/* zmienna określajaca liczbę dzieci (procesow potomnych) */
#define CHILDREN_COUNT 2

void on_child_usr1(int signal)
{
    printf("USR1 => Dziecko (PID = %d)\n", getpid());
}

/* zliczanie wynikow czastkowych */
void calculate_partial_results(const int child_id, const int vector_key, const int ranges_key, const int partial_results_key)
{
    int vector_shmid = shmget(vector_key, 0, 0666);
    double* vector = (double*)(shmat(vector_shmid, NULL, SHM_RDONLY));

    int ranges_shmid = shmget(ranges_key, 0, 0666);
    int* ranges = (int*)(shmat(ranges_shmid, NULL, SHM_RDONLY));

    int partial_results_shmid = shmget(partial_results_key, 0, 0666);
    double* partial_results = (double*)(shmat(partial_results_shmid, NULL, 0));

    partial_results[child_id] = 0.0;
    int i;
    for (i = ranges[child_id]; i < ranges[child_id+1]; i++) {
        partial_results[child_id] += vector[i];
    }

    printf("Dziecko: PID = %d, ID = %d, suma czesciowa = %lf\n", getpid(), child_id, partial_results[child_id]);

    shmdt(partial_results);
    shmdt(ranges);
    shmdt(vector);
}

/* generowanie dzieci */
void spawn_children(const int count, pid_t* pids, const int vector_key, const int ranges_key, const int partial_results_key)
{
    /* Przygotuj struktury dla obslugi przerwania */
    struct sigaction usr1;
    sigset_t mask;
    sigemptyset(&mask);
    usr1.sa_handler = (&on_child_usr1);
    usr1.sa_mask = mask;
    usr1.sa_flags = 0;

    pid_t pid;
    int i;
	printf("Tworze dzieci (%d)\n", CHILDREN_COUNT);
    for (i = 0; i < CHILDREN_COUNT; i++) {
        if ((pid = fork()) == 0) {
            sigaction(SIGUSR1, &usr1, NULL);
            printf("Rodzic przesyla USR1 do %d\n", getpid());
            pause();

            calculate_partial_results(i, vector_key, ranges_key, partial_results_key);
            exit(EXIT_SUCCESS);
        }
        else {
            pids[i] = pid;
        }
    }
}

/* sprzatanie dzieci */
void cleanup_children(const int children_spawned, const pid_t* children)
{
    int i;
    for (i = 0; i < children_spawned; i++) {
        kill(children[i], SIGTERM);
        wait(NULL);
    }
}

/* czyszczenie vector */
void cleanup_vector(const double* vector, const int vector_shmid, FILE* vector_data)
{
    if (vector != NULL) {
        shmdt(vector);
    }
    if (vector_shmid > 0) {
        shmctl(vector_shmid, IPC_RMID, 0);
    }
    if (vector_data != NULL) {
        fclose(vector_data);
    }
}


/* czyszczenie ranges */
void cleanup_ranges(const int* ranges, const int ranges_shmid)
{
    if (ranges != NULL) {
        shmdt(ranges);
    }
    if (ranges_shmid > 0) {
        shmctl(ranges_shmid, IPC_RMID, 0);
    }
}


/* czyszczenie partial_result */
void cleanup_partial_results(const double* partial_results, const int partial_results_shmid)
{
    if (partial_results != NULL) {
        shmdt(partial_results);
    }
    if (partial_results_shmid > 0) {
        shmctl(partial_results_shmid, IPC_RMID, 0);
    }
}


/* MAIN */
int main(int argc, char* argv[])
{
	clock_t start = clock();

    pid_t children_pids[CHILDREN_COUNT];
    FILE* vector_data;

    double* vector;
    int* ranges;
    double* partial_results;
    double summed_result;

    srand(time(NULL));
    int vector_key = rand() % 1000;
    int ranges_key = vector_key + 1;
    int partial_results_key = ranges_key + 1;

    int i;

    /* Utworz procesy potomne */
    spawn_children(CHILDREN_COUNT, children_pids, vector_key, ranges_key, partial_results_key);

    /* Otworz plik z vectorem */
	if (argc < 2)
	{
		printf("Podaj nazwe pliku zawierajacego vektor jako argument!\n");
		return EXIT_FAILURE;	
	}
    vector_data = fopen(argv[1], "r");
    if (vector_data == NULL) {
        fprintf(stderr, "Brak dostepu do pliku %s\n", argv[1]);
        cleanup_children(CHILDREN_COUNT, children_pids);
        return EXIT_FAILURE;
    }

    /* Wczytaj dlugosc vector_length */
    char buffer[BUFFER_LENGTH];
    int vector_length = 0;
	//char ch;    
	fgets(buffer, BUFFER_LENGTH, vector_data);
    sscanf(buffer, "%i", &vector_length);
	
	/*
	while ((ch = fgetc(vector_data)) != EOF)
    {
      	if (ch == '\n') {
   			vector_length++;
		}
    }
	*/
		
	//printf("liczba lini w pliku = %d\n", vector_length);

    /* Utworz shm dla vector */
    int vector_shmid = shmget(vector_key, sizeof(*vector)*vector_length, 0666|IPC_CREAT);
    if (vector_shmid == -1) {
        fprintf(stderr, "Blad w shmget() dla vector\n");
        cleanup_vector(NULL, 0, vector_data);
        return EXIT_FAILURE;
    }

    vector = (double*)(shmat(vector_shmid, NULL, 0));
    if (vector < 0) {
        fprintf(stderr, "Blad w shmat() dla vector\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(NULL, vector_shmid, vector_data);
        return EXIT_FAILURE;
    }
	
    /* Wczytaj wartości wektora */
	printf("Dlugosc wektora = %d\n", vector_length);
    for (i = 0; i < vector_length; i++) {
        fgets(buffer, BUFFER_LENGTH, vector_data);
        sscanf(buffer, "%lf", &(vector[i]));
        //printf("vector[%d] = %lf\n", i, vector[i]);
    }
    shmdt(vector);


    /* Utworz shm dla ranges (zakresy) */
    int ranges_shmid = shmget(ranges_key, sizeof(*ranges)*(CHILDREN_COUNT+1), 0666|IPC_CREAT);
    if (ranges_shmid == -1) {
        printf("Blad w shmget() dla ranges\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(vector, vector_shmid, vector_data);
        return EXIT_FAILURE;
    }

    ranges = (int*)(shmat(ranges_shmid, NULL, 0));
    if (ranges < 0) {
        printf("Blad w shmat() dla ranges\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(vector, vector_shmid, vector_data);
        cleanup_ranges(NULL, ranges_shmid);
        return EXIT_FAILURE;
    }

    /* Wypelnij ranges */
    int full_range_children = vector_length % CHILDREN_COUNT;
    ranges[0] = 0;
    for (i = 1; i <= full_range_children; i++){
        ranges[i] = ranges[i-1] + vector_length/CHILDREN_COUNT + 1;
    }
    for (; i <= CHILDREN_COUNT; i++) {
        ranges[i] = ranges[i-1] + vector_length/CHILDREN_COUNT;
    }
	/* Odblokowanie ranges */
    shmdt(ranges);


    /* Utworz shm dla partial_results */
    int partial_results_shmid = shmget(partial_results_key, sizeof(*partial_results)*CHILDREN_COUNT, 0666|IPC_CREAT);
    if (partial_results_shmid == -1) {
        printf("Blad w shmget() dla partial_results\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(vector, vector_shmid, vector_data);
        cleanup_ranges(ranges, ranges_shmid);
        return EXIT_FAILURE;
    }

    partial_results = (double*)(shmat(partial_results_shmid, NULL, 0));
    if (partial_results < 0) {
        printf("Blad w shmat() dla partial_results\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(vector, vector_shmid, vector_data);
        cleanup_ranges(ranges, ranges_shmid);
        cleanup_partial_results(NULL, partial_results_shmid);
        return EXIT_FAILURE;
    }

    /* Inicjuj partial_results */
    for (i = 0; i < CHILDREN_COUNT; i++) {
        partial_results[i] = 0.0;
    }
	/* Odblokowanie partial_results */
    shmdt(partial_results);

    /* Sumowanie */
    printf("Rodzic czeka %dsek na utworzenie dzieci\n", SLEEP_TIME);
    sleep(SLEEP_TIME);
    printf("Rodzic wysyła dzieciom USR1\n");
    for (i = 0; i < CHILDREN_COUNT; i++) {
        kill(children_pids[i], SIGUSR1);
    }
    printf("===>    Rodzic czeka na dzieci [liczba = %d]\n", CHILDREN_COUNT);
    for (i = 0; i < CHILDREN_COUNT; i++) {
        wait(NULL);
    }
    printf("Rodzic: koniec czekania.\n");

    partial_results = (double*)(shmat(partial_results_shmid, NULL, 0));
    if (partial_results < 0) {
        printf("Blad w shmat() dla sumowania partial_results\n");
        cleanup_children(CHILDREN_COUNT, children_pids);
        cleanup_vector(vector, vector_shmid, vector_data);
        cleanup_ranges(ranges, ranges_shmid);
        cleanup_partial_results(NULL, partial_results_shmid);
        return EXIT_FAILURE;
    }

    summed_result = 0.0;
    for (i = 0; i < CHILDREN_COUNT; i++) {
        summed_result += partial_results[i];
    }
    printf("===>    Suma wektora = %lf\n", summed_result);
	/* czyszczenie koncowe */
    cleanup_children(CHILDREN_COUNT, children_pids);
    cleanup_vector(vector, vector_shmid, vector_data);
    cleanup_ranges(ranges, ranges_shmid);
    cleanup_partial_results(partial_results, partial_results_shmid);
	printf("===>    Czas wykonywania: %lu ms\n", clock() - start );
    return EXIT_SUCCESS;
}
