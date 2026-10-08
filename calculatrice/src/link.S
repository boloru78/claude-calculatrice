/* link.S — Appels système de la calculatrice pour le port série.
 *
 * gint ne pilote pas encore le port 3 broches : on utilise les fonctions du
 * système Casio, appelées pendant un gint_world_switch() (voir
 * platform_gint.c). Numéros d'après la liste de SimLo (« fx legacy
 * syscalls ») ; appel par la table des syscalls en 0x80010070, comme dans
 * gint (src/kernel/syscalls.S). */

.global _Serial_Open
.global _Serial_Close
.global _Serial_ReadOneByte
.global _Serial_BufferedTransmitOneByte
.global _Serial_ClearReceiveBuffer
.global _RTC_GetTicks

.section .text

#define syscall(id)         \
	mov.l	1f, r2      ;\
	mov.l	2f, r0      ;\
	jmp	@r2         ;\
	nop                 ;\
.balign 4                   ;\
1:	.long	0x80010070  ;\
2:	.long	id

/* int Serial_Open(unsigned char *mode) : 0 si ouvert, 3 si déjà ouvert */
_Serial_Open:
	syscall(0x418)

/* int Serial_Close(int mode) : mode 1 = fermer tout de suite */
_Serial_Close:
	syscall(0x419)

/* int Serial_ReadOneByte(unsigned char *byte) : 0 si un octet est lu,
   1 si rien n'est arrivé */
_Serial_ReadOneByte:
	syscall(0x40c)

/* int Serial_BufferedTransmitOneByte(unsigned char byte) : 0 si accepté,
   non nul si le tampon d'envoi est plein */
_Serial_BufferedTransmitOneByte:
	syscall(0x40e)

/* int Serial_ClearReceiveBuffer(void) */
_Serial_ClearReceiveBuffer:
	syscall(0x413)

/* int RTC_GetTicks(void) : tics de 1/128 s */
_RTC_GetTicks:
	syscall(0x03b)
