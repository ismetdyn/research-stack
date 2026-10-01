import argparse
import sys


parser = argparse.ArgumentParser()
parser.add_argument("--operation", choices=("search", "discover", "download"), default="search")
parser.add_argument("--query")
parser.add_argument("--limit")
parser.add_argument("--source", action="append")
parser.add_argument("--title")
parser.add_argument("--doi")
parser.add_argument("--open-access-url")
parser.add_argument("--candidate-url")
parser.add_argument("--candidate-source")
args = parser.parse_args()

if args.operation == "discover":
    if args.title != "Full text title" or args.doi != "10.1000/example":
        raise SystemExit(1)
    sys.stdout.write(
        "HARVESTER_RESEARCH_V2\n"
        "CANDIDATE\taHR0cHM6Ly9yZXBvc2l0b3J5LmV4YW1wbGUvcGFwZXIucGRm\tT3BlbkFsZXg=\tRXhhbXBsZSBSZXBvc2l0b3J5\tQ0MtQlktNC4w\tYWNjZXB0ZWRWZXJzaW9u\tcmVwb3NpdG9yeQ==\t1\n"
        "END\n"
    )
elif args.operation == "download":
    if (
        args.title != "Full text title"
        or args.candidate_url != "https://repository.example/paper.pdf"
        or args.candidate_source != "OpenAlex"
    ):
        raise SystemExit(1)
    sys.stdout.write(
        "HARVESTER_RESEARCH_V2\n"
        "DOWNLOAD\tcGFwZXItYTFiMmMzZDRlNWY2LnBkZg==\t42\t"
        "a1b2c3d4e5f60718293a4b5c6d7e8f90123456789abcdef0123456789abcdef0\n"
        "END\n"
    )
elif args.query == "valid":
    sys.stdout.write(
        "HARVESTER_RESEARCH_V1\n"
        "WORK\td29yazox\tVGl0bGU=\tQWRh\tT3BlbkFsZXg=\t2026\t9\n"
        "END\n"
    )
elif args.query == "malformed":
    sys.stdout.write("not the worker protocol\n")
elif args.query == "oversized":
    sys.stdout.write("x" * (1024 * 1024 + 1))
elif args.query == "worker-error":
    sys.stdout.write("ERROR\tU2VsZWN0ZWQgc291cmNlcyBkaWQgbm90IHJlc3BvbmQu\n")
    raise SystemExit(1)
else:
    raise SystemExit(1)
