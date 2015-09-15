#include "lex.h"
#include "parse.h"
#include "ast.h"
#include "scope.h"
#include "type.h"
#include "codegen.h"
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int fd;
    size_t size;
    struct stat statbuf;
    int exit_status = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        goto out_exit;
    }

    if ((fd = open(argv[1], O_RDONLY)) < 0) {
        perror("open");
        goto out_exit;
    }

    if (fstat(fd, &statbuf) < 0) {
        perror("fstat");
        goto out_close;
    }

    if ((size = (size_t) statbuf.st_size) == 0) {
        fprintf(stderr, "‘%s‘: file is empty\n", argv[1]);
        goto out_close;
    }

    const uint8_t *mapped = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (mapped == MAP_FAILED) {
        perror("mmap");
        goto out_close;
    }

    struct lex_token *tokens;
    size_t ntokens;
    lex_current_file = argv[1];

    if (lex(mapped, size, &tokens, &ntokens)) {
        goto out_destroy_tokens;
    }

    const struct parse_node root = parse(tokens, ntokens);

    if (parse_error(root)) {
        goto out_destroy_tokens;
    }

    struct ast_node *ast;
    const int ast_error = ast_build(&root, &ast);
    parse_tree_destroy(root);

    if (ast_error == AST_NOMEM) {
        goto out_destroy_ast;
    }

    const int scope_error = scope_build(ast);

    if (scope_error == SCOPE_NOMEM) {
        goto out_destroy_ast;
    }

    const int type_check_error = type_check_ast(ast);
    //ast_print(stdout, ast, 0);

    if (ast_error || scope_error || type_check_error) {
        goto out_destroy_ast;
    }

    struct codegen_obj obj;
    const int codegen_error = codegen_obj_create(ast, &obj);

    if (codegen_error) {
        goto out_destroy_obj;
    }

    type_symtab_clear();
    ast_destroy(ast), ast = NULL;
    free(tokens), tokens = NULL;
    munmap((uint8_t *) mapped, size), mapped = NULL;
    close(fd), fd = -1;
    exit_status = exec(&obj);

out_destroy_obj:
    codegen_obj_destroy(&obj);

out_destroy_ast:
    type_symtab_clear();
    ast_destroy(ast);

out_destroy_tokens:
    free(tokens);

    if (mapped) {
        munmap((uint8_t *) mapped, size);
    }

out_close:
    if (fd != -1) {
        close(fd);
    }

out_exit:
    return exit_status;
}
