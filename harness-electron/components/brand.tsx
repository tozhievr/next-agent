export function Brand({ className = "size-6" }: { className?: string }) {
  return (
    <svg
      className={className}
      viewBox="0 0 256 256"
      fill="none"
      aria-hidden="true"
    >
      <path
        d="M64 181V75a11 11 0 0 1 19-7l86 99V75"
        stroke="currentColor"
        strokeWidth="25"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M155 181h26v-26"
        stroke="currentColor"
        strokeWidth="13"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}
